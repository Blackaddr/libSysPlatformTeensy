#include "imxrt.h"
#include "SysTypes.h"
#include "SysLogger.h"
#include "SysDebugPrint.h"
#include "SysCrashReport.h"

namespace SysPlatform {

SysCrashReport sysCrashReport;
bool SysCrashReport::m_enabled = true;

/* Crash report info stored in the top 128 bytes of OCRAM (at 0x2027FF80)
struct arm_fault_info_struct {
        uint32_t len;
        uint32_t ipsr;
        uint32_t cfsr;
        uint32_t hfsr;
        uint32_t mmfar;
        uint32_t bfar;
        uint32_t ret;
        uint32_t xpsr;
        float  temp;
        uint32_t time;
        uint32_t crc;
}; */
extern unsigned long _ebss;
extern "C" bool temperature_is_safe(void);
extern "C" uint32_t set_arm_clock(uint32_t frequency); // clockspeed.c

static int isvalid(const struct arm_fault_info_struct *info);
static void cleardata(struct arm_fault_info_struct *info);

FLASHMEM
size_t SysCrashReport::printReport() const
{
  if (!m_enabled) { return 0; }
  if (!isSysDebugPrintEnabled()) { return 0; }
  volatile struct arm_fault_info_struct *info = (struct arm_fault_info_struct *)0x2027FF80;
  volatile struct crashreport_breadcrumbs_struct *bc = (struct crashreport_breadcrumbs_struct *)0x2027FFC0;
  arm_dcache_flush_delete((void *)bc, sizeof(struct crashreport_breadcrumbs_struct));

  if (isvalid(info)) {
    sysLogger.printf("\n\n!!!CrashReport:");
    uint8_t ss = info->time % 60;
    info->time /= 60;
    uint8_t mm = info->time % 60;
    info->time /= 60;
    uint8_t hh = info->time % 24;
    sysLogger.printf("  A problem occurred at (system time): %2d:%2d:%2d\n", hh, mm, ss);
    sysLogger.printf("  Code was executing from address %p\n", info->ret);

    sysLogger.printf("  Temperature inside the chip was %f °C\n ", info->temp);

  } else {
    sysLogger.printf("No Crash Data To Report\n");
    sysLogger.printf("  Hopefully all is well, but certain types of crashes can't be reported:\n");
    sysLogger.printf("\tstuck in an infinite loop (technically, hardware still running properly)\n");
    sysLogger.printf("\tremaining in a low power sleep mode\n");
    sysLogger.printf("\taccess to certain peripherals without their clock enabled (eg, FlexIO)\n");
    sysLogger.printf("\tchange of CPU or bus clock speed without use of glitchless mux\n");
  }
  uint32_t SRSR = SRC_SRSR;
  if (SRSR & SRC_SRSR_LOCKUP_SYSRESETREQ) {
    // use SRC_GPR5 to distinguish cases.  See pages 1290 & 1294 in ref manual
    uint32_t gpr5 = SRC_GPR5;
    if (gpr5 == 0x0BAD00F1) {
      sysLogger.printf("  Reboot was caused by auto reboot after fault or bad interrupt detected\n");
    } else {
      sysLogger.printf("  Reboot was caused by software write to SCB_AIRCR or CPU lockup\n");
    }
  }
  if (SRSR & SRC_SRSR_CSU_RESET_B) {
    sysLogger.printf("  Reboot was caused by security monitor\n");
  }
  if (SRSR & SRC_SRSR_IPP_USER_RESET_B) {
    // This case probably can't occur on Teensy 4.x
    // because the bootloader chip monitors 3.3V power
    // and manages DCDC_PSWITCH and RESET, causing the
    // power on event to appear as a normal reset.
    sysLogger.printf("  Reboot was caused by power on/off button\n");
  }
  if (SRSR & SRC_SRSR_WDOG_RST_B) {
    sysLogger.printf("  Reboot was caused by watchdog 1 or 2\n");
  }
  if (SRSR & SRC_SRSR_JTAG_RST_B) {
    sysLogger.printf("  Reboot was caused by JTAG boundary scan\n");
  }
  if (SRSR & SRC_SRSR_JTAG_SW_RST) {
    sysLogger.printf("  Reboot was caused by JTAG debug\n");
  }
  if (SRSR & SRC_SRSR_WDOG3_RST_B) {
    sysLogger.printf("  Reboot was caused by watchdog 3\n");
  }
  if (SRSR & SRC_SRSR_TEMPSENSE_RST_B) {
    sysLogger.printf("  Reboot was caused by temperature sensor\n");
	  SRC_SRSR &= ~0x100u; /* Write 0 to clear. */
	  sysLogger.printf("Panic Temp Exceeded Shutting Down\n");
	  sysLogger.printf("Can be caused by Overclocking w/o Heatsink or other unknown reason\n");
	  IOMUXC_GPR_GPR16 = 0x00000007;
	  SNVS_LPCR |= SNVS_LPCR_TOP; //Switch off now
	  asm volatile ("dsb":::"memory");
	  while (1) asm ("wfi");
  }
  if (bc->bitmask && bc->checksum == checksum(bc, 28)) {
    for (unsigned i=0; i < NUM_BREADCRUMBS; i++) {
      if (bc->bitmask & (1 << i)) {
        sysLogger.printf("  Breadcumb #%d was %d (0x%08X)\n", i, bc->value[i], bc->value[i]);
      }
      bc->value[i] = INVALID_BREADCRUMB;  // clear the breadcrumb
    }
  }
  clear();
  return 1;
}

FLASHMEM
void SysCrashReport::disable()
{
  reset();
  m_enabled = false;
}

FLASHMEM
void SysCrashReport::enable()
{
  m_enabled = true;
  reset();
}

FLASHMEM
void SysCrashReport::reset()
{
    struct crashreport_breadcrumbs_struct *bc = (struct crashreport_breadcrumbs_struct *)0x2027FFC0;
    clear();
    for (unsigned i=0; i < NUM_BREADCRUMBS; i++) {
      bc->value[i] = INVALID_BREADCRUMB;  // clear the breadcrumb
    }
}

FLASHMEM
void SysCrashReport::clear()
{
  struct arm_fault_info_struct *info = (struct arm_fault_info_struct *)0x2027FF80;
  cleardata(info);
  struct crashreport_breadcrumbs_struct *bc = (struct crashreport_breadcrumbs_struct *)0x2027FFC0;
  *(volatile uint32_t *)(&bc->bitmask) = 0;
  *(volatile uint32_t *)(&bc->checksum) = checksum(bc, 28);
  arm_dcache_flush((void *)bc, sizeof(struct crashreport_breadcrumbs_struct));
}

FLASHMEM
SysCrashReport::operator bool()
{
  if (!m_enabled) { return false; }
	struct arm_fault_info_struct *info = (struct arm_fault_info_struct *)0x2027FF80;
	if (isvalid(info)) return true;
	return false;
}

FLASHMEM
static int isvalid(const struct arm_fault_info_struct *info)
{
	uint32_t i, crc;
	const uint32_t *data, *end;

	if (info->len != sizeof(*info) / 4) return 0;
	data = (uint32_t *)info;
	end = data + (sizeof(*info) / 4 - 1);
	crc = 0xFFFFFFFF;
	while (data < end) {
		crc ^= *data++;
		for (i=0; i < 32; i++) crc = (crc >> 1) ^ (crc & 1)*0xEDB88320;
	}
	if (crc != info->crc) return 0;
	return 1;
}

FLASHMEM
static void cleardata(struct arm_fault_info_struct *info)
{
	info->len = 0;
	info->ipsr  = 0;
	info->cfsr  = 0;
	info->hfsr  = 0;
	info->mmfar = 0;
	info->bfar  = 0;
	info->ret = 0;
	info->xpsr  = 0;
	info->crc = 0;
	arm_dcache_flush_delete(info, sizeof(*info));
	SRC_SRSR = SRC_SRSR; // zeros all write-1-to-clear bits
	SRC_GPR5 = 0;
}

void SysCrashReport::setBreadcrumb(unsigned num, uint32_t mask, uint32_t value)
{
    if (!m_enabled) { return; }
    breadcrumbRaw(num, mask | value);
}

void SysCrashReport::breadcrumbRaw(unsigned int num, unsigned int value) {
    if (!m_enabled) { return; }

    // crashreport_breadcrumbs_struct occupies exactly 1 cache row
    volatile struct crashreport_breadcrumbs_struct *bc =
        (struct crashreport_breadcrumbs_struct *)0x2027FFC0;
    if (num >= 1 && num <= NUM_BREADCRUMBS) {
        num--;
        bc->value[num] = value;
        bc->bitmask |= (1 << num);
        bc->checksum = checksum(bc, 28);
        arm_dcache_flush_delete((void *)bc, sizeof(struct crashreport_breadcrumbs_struct));
    }
}

uint32_t SysCrashReport::getBreadcrumb(unsigned num)
{
  volatile struct crashreport_breadcrumbs_struct *bc =
        (struct crashreport_breadcrumbs_struct *)0x2027FFC0;

  num--;
  if (bc->bitmask && bc->checksum == checksum(bc, 28)) {
    return bc->value[num];
  } else { return 0xFFFF'FFFFU; } // invalid breadcrumb
}

uint32_t SysCrashReport::checksum(volatile const void *data, int len) {
    volatile const uint16_t *p = (volatile const uint16_t *)data;
    uint32_t a=1, b=0; // Adler Fletcher kinda, len < 720 bytes
    while (len > 0) {
        a += *p++;
        b += a;
        len -= 2;
    }
    a = a & 65535;
    b = b & 65535;
    return a | (b << 16);
}

}
