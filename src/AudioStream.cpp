/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdlib.h>
#include <cstring>  // for std::memset()
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysCpuControl.h"
#include "sysPlatform/SysTimer.h"
#include "sysPlatform/SysWatchdog.h"
#include "sysPlatform/SysCrashReport.h"
#include "sysPlatform/SysLogger.h"
#include "AudioStream.h"

using namespace SysPlatform;

#if defined(__IMXRT1062__)
  #define MAX_AUDIO_MEMORY 229376
#else
  #define MAX_AUDIO_MEMORY 229376
#endif

#define NUM_MASKS  (((MAX_AUDIO_MEMORY / AUDIO_BLOCK_SAMPLES / 2) + 31) / 32)

extern const unsigned AUDIO_SAMPLES_PER_BLOCK = AUDIO_BLOCK_SAMPLES;
extern const float    AUDIO_SAMPLE_RATE_HZ    = AUDIO_SAMPLE_RATE_EXACT;
extern const float    AUDIO_SAMPLE_PERIOD_SEC = AUDIO_SAMPLE_PERIOD_SEC_F;

std::atomic<bool> audioIsrInProgress(false);
volatile int8_t audio_traversal_array[MAX_TRAVERSAL_BYTES];

audio_block_float32_t * AudioStream::memory_pool;
uint32_t AudioStream::memory_pool_available_mask[NUM_MASKS];
uint16_t AudioStream::memory_pool_first_mask;

uint16_t AudioStream::cpu_cycles_total = 0;
uint16_t AudioStream::cpu_cycles_total_max = 0;
uint16_t AudioStream::memory_used = 0;
uint16_t AudioStream::memory_used_max = 0;
uint16_t AudioStream::num_buffers = 0;
AudioConnection* AudioStream::unused = NULL; // linked list of unused but not destructed connections

bool AudioStream::use_ordered_update = false;
AudioStream* AudioStream::step_update_object = nullptr;
AudioStream** AudioStream::ordered_update_array = nullptr;

void software_isr(void);


// Set up the pool of audio data blocks
// placing them all onto the free list
FLASHMEM void AudioStream::initialize_memory(audio_block_float32_t *data, unsigned int num, float *dataBuffers)
{
	unsigned int i;
	unsigned int maxnum = MAX_AUDIO_MEMORY / AUDIO_BLOCK_SAMPLES / sizeof(float);

	//Serial.println("AudioStream initialize_memory");
	//delay(10);
	if (num > maxnum) num = maxnum;
	num_buffers = num;
	SysCpuControl::disableIrqs();
	memory_pool = data;
	memory_pool_first_mask = 0;
	for (i=0; i < NUM_MASKS; i++) {
		memory_pool_available_mask[i] = 0;
	}
	for (i=0; i < num; i++) {
		memory_pool_available_mask[i >> 5] |= (1 << (i & 0x1F));
	}
	for (i=0; i < num; i++) {
		data[i].memory_pool_index = i;
		if (dataBuffers) {
			data[i].data = dataBuffers + i*AUDIO_BLOCK_SAMPLES;
		}
	}

	AudioMemoryUsageMaxReset();
#if 0 // disable timer support. For STRIDE, we will always use i2sIn interrupt timing.
	if (update_scheduled == false) {
		// if no hardware I/O has taken responsibility for update,
		// start a timer which will call update_all() at the correct rate
		SysIntervalTimer *timer = new SysIntervalTimer();
		if (timer) {
			float usec = 1e6 * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE_EXACT;
			timer->begin(update_all, usec);
			update_setup();
		}
	}
#endif
    std::memset((void*)audio_traversal_array, 255, MAX_TRAVERSAL_BYTES);  // initialize to -1
	SysCpuControl::enableIrqs();
}

// Allocate 1 audio data block.  If successful
// the caller is the only owner of this new block
audio_block_t * AudioStream::allocate(void)
{
	audio_block_t* block = (audio_block_t*)allocateFloat();
	block->flags = 0;
	return block;
}

audio_block_float32_t * AudioStream::allocateFloat(void)
{
	uint32_t n, index, avail;
	uint32_t *p, *end;
	audio_block_float32_t *block;
	uint32_t used;

	p = memory_pool_available_mask;
	end = p + NUM_MASKS;
	SysCpuControl::disableIrqs();
	index = memory_pool_first_mask;
	p += index;
	// calculate the number of free masks
	while (1) {
		if (p >= end) {
			SysCpuControl::enableIrqs();
			SYS_DEBUG_PRINT(sysLogger.printf("AudioStream::allocateFloat(): FAILURE!!! NUM_MASKS=%d  memory_pool_first_mask=%d  index=%d\n",
			    NUM_MASKS, memory_pool_first_mask, index));
			return NULL;
		}
		avail = *p;
		if (avail) break;
		index++;
		p++;
	}
	n = __builtin_clz(avail);
	avail &= ~(0x80000000 >> n);
	*p = avail;
	if (!avail) index++;
	memory_pool_first_mask = index;
	used = memory_used + 1;
	memory_used = used;
	SysCpuControl::enableIrqs();
	index = p - memory_pool_available_mask;
	//audio_block_float32_t* blockInit = memory_pool + ((index << 5) + (31 - n));
	//block = (audio_block_t*)blockInit;.
	block = memory_pool + ((index << 5) + (31 - n));
	block->ref_count = 1;
	if (used > memory_used_max) memory_used_max = used;
	//Serial.print("alloc:");
	//Serial.println((uint32_t)block, HEX);
	block->flags = FLOAT_MASK;
	return block;
}

// Release ownership of a data block.  If no
// other streams have ownership, the block is
// returned to the free pool
void AudioStream::release(audio_block_t *block)
{
	return release((audio_block_float32_t*)block);
}

void AudioStream::release(audio_block_t** block)
{
	return release((audio_block_float32_t**)block);
}

void AudioStream::release(audio_block_float32_t *block)
{
	if (block == nullptr) return;
	uint32_t mask = (0x80000000 >> (31 - (block->memory_pool_index & 0x1F)));
	uint32_t index = block->memory_pool_index >> 5;

	SysCpuControl::disableIrqs();
	if (block->ref_count > 1) {
		block->ref_count--;
	} else if (block->ref_count == 1) {
		//Serial.print("reles:");
		//Serial.println((uint32_t)block, HEX);
		memory_pool_available_mask[index] |= mask;
		if (index < memory_pool_first_mask) memory_pool_first_mask = index;
		if (memory_used == 0) {
			SYS_DEBUG_PRINT(sysLogger.printf("AudioStream::release(): WARNING: release() called when memory_used:%d\n", memory_used));
		} else {
			memory_used--;
		}
	}
	SysCpuControl::enableIrqs();
}

void AudioStream::release(audio_block_float32_t** block)
{
	if (!block) { return; }
	if (*block) { release(*block); *block = nullptr; }
}

void AudioStream::releaseAll()
{
	memory_pool_first_mask = 0;
	for (unsigned i=0; i < NUM_MASKS; i++) {
		memory_pool_available_mask[i] = 0;
	}
	for (unsigned i=0; i < num_buffers; i++) {
		memory_pool_available_mask[i >> 5] |= (1 << (i & 0x1F));
	}
	for (unsigned i=0; i < num_buffers; i++) {
		memory_pool[i].ref_count = 0;
		memory_pool[i].flags = 0;
	};
	memory_used = 0;
}

// Transmit an audio data block
// to all streams that connect to an output.  The block
// becomes owned by all the recepients, but also is still
// owned by this object.  Normally, a block must be released
// by the caller after it's transmitted.  This allows the
// caller to transmit to same block to more than 1 output,
// and then release it once after all transmit calls.
void AudioStream::transmit(audio_block_t *block, unsigned char index)
{
	return transmit((audio_block_float32_t*)block, index);
}

void AudioStream::transmit(audio_block_float32_t *block, unsigned char index)
{
	for (AudioConnection *c = destination_list; c != NULL; c = c->next_dest) {
		if (c->src_index == index) {
			if (c->dst->inputQueue[c->dest_index] == NULL) {
				c->dst->inputQueue[c->dest_index] = block;
				block->ref_count++;
			}
		}
	}
}


// Receive block from an input.  The block's data
// may be shared with other streams, so it must not be written
audio_block_t * AudioStream::receiveReadOnly(unsigned int index)
{
	return ((audio_block_t*)receiveReadOnlyFloat(index));
}
audio_block_float32_t * AudioStream::receiveReadOnlyFloat(unsigned int index)
{
	audio_block_float32_t *in;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;
	return in;
}

// Receive block from an input.  The block will not
// be shared, so its contents may be changed.
audio_block_t * AudioStream::receiveWritable(unsigned int index)
{
	audio_block_float32_t *in, *p;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;
	if (in && in->ref_count > 1) {
		p = (audio_block_float32_t*)allocate();
		if (p) memcpy(p->data, in->data, sizeof(int16_t) * AUDIO_BLOCK_SAMPLES);
		in->ref_count--;
		in = p;
	}
	return (audio_block_t*)in;
}

audio_block_float32_t * AudioStream::receiveWritableFloat(unsigned int index)
{
	audio_block_float32_t *in, *p;

	if (index >= num_inputs) return NULL;
	in = inputQueue[index];
	inputQueue[index] = NULL;
	if (in && in->ref_count > 1) {
		p = (audio_block_float32_t*)allocateFloat();
		if (p) memcpy(p->data, in->data, sizeof(float) * AUDIO_BLOCK_SAMPLES);
		in->ref_count--;
		in = p;
	}
	return in;
}

/**************************************************************************************/
// Full constructor with 4 parameters
AudioConnection::AudioConnection(AudioStream &source, unsigned char sourceOutput,
		AudioStream &destination, unsigned char destinationInput)
{
	// we are effectively unused right now, so
	// link ourselves at the start of the unused list
	next_dest = AudioStream::unused;
	AudioStream::unused = this;

	isConnected = false;
	connect(source,sourceOutput,destination,destinationInput);
}

// Simplified constructor assuming channel 0 at both ends
AudioConnection::AudioConnection(AudioStream &source, AudioStream &destination)
{
	next_dest = AudioStream::unused;
	AudioStream::unused = this;

	isConnected = false;
	connect(source, 0, destination,0);
}

// Destructor
AudioConnection::~AudioConnection()
{
	AudioConnection** pp;

	disconnect(); // disconnect ourselves: puts us on the unused list
	// Remove ourselves from the unused list
	pp = &AudioStream::unused;
	while (*pp && *pp != this)
		pp = &((*pp)->next_dest);
	if (*pp) // found ourselves
		*pp = next_dest; // remove ourselves from the unused list
}

/**************************************************************************************/
int AudioConnection::connect(void)
{
	int result = 1;
	AudioConnection *p;
	AudioConnection **pp;
	AudioStream* s;

	do
	{
		if (isConnected) // already connected
		{
			break;
		}

		if (!src || !dst) // NULL src or dst - [old] Stream object destroyed
		{
			result = 3;
			break;
		}

		if (dest_index >= dst->num_inputs) // input number too high
		{
			result = 2;
			break;
		}

		SysCpuControl::disableIrqs();

		// First check the destination's input isn't already in use
		s = AudioStream::first_update; // first AudioStream in the stream list
		while (s) // go through all AudioStream objects
		{
			p = s->destination_list;	// first patchCord in this stream's list
			while (p)
			{
				if (p->dst == dst && p->dest_index == dest_index) // same destination - it's in use!
				{
					SysCpuControl::enableIrqs();
					return 4;
				}
				p = p->next_dest;
			}
			s = s->next_update;
		}

		// Check we're on the unused list
		pp = &AudioStream::unused;
		while (*pp && *pp != this)
		{
			pp = &((*pp)->next_dest);
		}
		if (!*pp) // never found ourselves - fail
		{
			result = 5;
			break;
		}

		// Now try to add this connection to the source's destination list
		p = src->destination_list; // first AudioConnection
		if (p == NULL)
		{
			src->destination_list = this;
		}
		else
		{
			while (p->next_dest)  // scan source Stream's connection list for duplicates
			{

				if (&p->src == &this->src && &p->dst == &this->dst
					&& p->src_index == this->src_index && p->dest_index == this->dest_index)
				{
					//Source and destination already connected through another connection, abort
					SysCpuControl::enableIrqs();
					return 6;
				}
				p = p->next_dest;
			}

			p->next_dest = this; // end of list, can link ourselves in
		}

		*pp = next_dest;  // remove ourselves from the unused list
		next_dest = NULL; // we're last in the source's destination list

		src->numConnections++;
		src->active = true;

		dst->numConnections++;
		dst->active = true;

		isConnected = true;

		result = 0;
	} while (0);

	SysCpuControl::enableIrqs();

	return result;
}


int AudioConnection::connect(AudioStream &source, unsigned char sourceOutput,
		AudioStream &destination, unsigned char destinationInput)
{
	int result = 1;

	if (!isConnected)
	{
		src = &source;
		dst = &destination;
		src_index = sourceOutput;
		dest_index = destinationInput;

		result = connect();
	}
	return result;
}

int AudioConnection::disconnect(void)
{
	AudioConnection *p;

	if (!isConnected) return 1;
	if (dest_index >= dst->num_inputs) return 2; // should never happen!
	SysCpuControl::disableIrqs();

	// Remove destination from source list
	p = src->destination_list;
	if (p == NULL) {
//>>> PAH re-enable the IRQ
		SysCpuControl::enableIrqs();
		return 3;
	} else if (p == this) {
		if (p->next_dest) {
			src->destination_list = next_dest;
		} else {
			src->destination_list = NULL;
		}
	} else {
		while (p)
		{
			if (p->next_dest == this) // found the parent of the disconnecting object
			{
				p-> next_dest = this->next_dest; // skip parent's link past us
				break;
			}
			else
				p = p->next_dest; // carry on down the list
		}
	}
//>>> PAH release the audio buffer properly
	//Remove possible pending src block from destination
	if(dst->inputQueue[dest_index] != NULL) {
		AudioStream::release((audio_block_t*)dst->inputQueue[dest_index]);
		// release() re-enables the IRQ. Need it to be disabled a little longer
		SysCpuControl::disableIrqs();
		dst->inputQueue[dest_index] = NULL;
	}

	//Check if the disconnected AudioStream objects should still be active
	src->numConnections--;
	if (src->numConnections == 0) {
		src->active = false;
	}

	dst->numConnections--;
	if (dst->numConnections == 0) {
		dst->active = false;
	}

	isConnected = false;
	next_dest = dst->unused;
	dst->unused = this;

	SysCpuControl::enableIrqs();

	return 0;
}


// When an object has taken responsibility for calling update_all()
// at each block interval (approx 2.9ms), this variable is set to
// true.  Objects that are capable of calling update_all(), typically
// input and output based on interrupts, must check this variable in
// their constructors.
bool AudioStream::update_scheduled = false;

bool AudioStream::update_setup(void)
{
	if (update_scheduled) return false;
	SysCpuControl::AudioAttachInterruptVector(software_isr);
	SysCpuControl::AudioSetInterruptPriority(208); // 255 = lowest priority
	SysCpuControl::AudioInterrupts();
	update_scheduled = true;
	return true;
}

void AudioStream::update_stop(void)
{
	SysCpuControl::AudioNoInterrupts();
	update_scheduled = false;
}

void AudioStream::update_all(void) {
	SysCpuControl::AudioTriggerInterrupt();
}

void AudioStream::setOrderedUpdate(bool orderedUpdate) {

	use_ordered_update = orderedUpdate;
	if (!use_ordered_update || ordered_update_array) { return; }

	int idx = 0;
	int num_objects = 0;
	AudioStream *p = AudioStream::first_update;
	for (p = AudioStream::first_update; p; p = p->next_update) { num_objects++; }  // get the number of objects

	ordered_update_array = (AudioStream**)malloc(num_objects * sizeof(AudioStream*));
	if (!ordered_update_array) {
		//if (Serial) { Serial.printf("AudioStream::setOrderedUpdate(): unable to allocate array\n"); Serial.flush(); }
		return;
	}

	// populate the ordered list
	step_update_object = nullptr;
	unsigned objects_remaining = num_objects;
	unsigned loopLimit = 100;
	while ((objects_remaining > 0) && (loopLimit-- > 0)) {
		for (p = AudioStream::first_update; p; p = p->next_update) {
			//if (Serial) { Serial.printf("setOrderedUpdate(): scanning object %d, looking for %d, objects remaining: %d\n", p->id, idx, objects_remaining); Serial.flush(); }
			if ((p->id == UPDATE_STEP_OBJECT_ID) && (!step_update_object)) {
				step_update_object = p;
				objects_remaining--;
			} else if (p->id == idx) {
				ordered_update_array[idx] = p;
				idx++;
				objects_remaining--;
			}
		}
	}
	if ((!step_update_object) || (loopLimit == 0)) {
		//if (Serial && !step_update_object) { Serial.printf("ERROR: step_update_object is not valid\n"); }
		//if (Serial && (loopLimit == 0)) { Serial.printf("ERROR: step_update_object is not valid\n"); }
		orderedUpdate = false;
	}
	ordered_update_array[idx] = nullptr; // terminate the list

	// if (Serial) {
	// 	Serial.printf("\n*** AUDIOSTREAM ARRAY\n");
	// 	for (int i=0; i < num_objects; i++) {
	// 		if (ordered_update_array[i]) {
	// 			Serial.printf("idx:%d  id:%d  Addr:0x%08X\n", i, ordered_update_array[i]->id, ordered_update_array[i]);
	// 		} else { Serial.printf("idx:%d  Addr:0x%08X\n", i, ordered_update_array[i]);}
	// 	}
	// 	Serial.printf("\nNum objects:%d  StepObject:%d\n", idx, step_update_object ? true : false);
	// }
}

AudioStream * AudioStream::first_update = NULL;

void software_isr(void) // AudioStream::update_all()
{
	AudioStream *p;

	uint32_t totalcycles = SysTimer::cycleCnt32();
	//digitalWriteFast(2, HIGH);

    if (AudioStream::use_ordered_update) {
		// with ordered update mode, we walk through the audio_traversal_array.
		// Each entry is a pair of integers. The first number is the update index to call
		// with the step_update_object. Typically this is a X-point switch. The second number
		// is the AudioStream ID, which is not used when calling the step_update_object.
		// If the first number is negative, then the call is to the AudioStream ID provided
		// as the second number.
		// If both numbers are negative, we are done.
		//if (!AudioStream::step_update_object) { AudioStream::use_ordered_update = false; return; }
		unsigned idx = 0;
		unsigned loopLimit = 50;

        //if (Serial) { Serial.printf("-\n"); }
		audioIsrInProgress = true;
        while(loopLimit-- > 0) {

			int8_t stepIndex  = audio_traversal_array[idx];
			int8_t objectId = audio_traversal_array[idx+1];
			if (stepIndex == -1 && objectId == -1) { break; }  // we are done
			idx += 2;

			if ( ((stepIndex) >= 0) && AudioStream::step_update_object) {  // run the update step object first
			    p = AudioStream::step_update_object;
				//Serial.printf("Step:%d id:%d addr:%08X\n", stepIndex, objectId, p);
				if (p->active) {
					uint32_t cycles = SysTimer::cycleCnt32();
					p->updateIndex(stepIndex);
					cycles = (SysTimer::cycleCnt32() - cycles) >> 6;
					p->cpu_cycles = cycles;
					if (cycles > p->cpu_cycles_max) p->cpu_cycles_max = cycles;
				}
			} else {  // run the update on the AudioStream ID object
				if (objectId >= 0) {
					p = AudioStream::ordered_update_array[objectId];
					//Serial.printf("id:%d addr:%08X\n", objectId, p);
					if (!p) {
						//if (Serial) { Serial.printf("software_isr(): null AudioStream object encountered\n"); }
						continue;
					}
					if (p->active) {
						sysCrashReport.setBreadcrumb(SysCrashReport::AUDIO_EFFECT_UPDATE_ID, SysCrashReport::START_MASK, (uint32_t)(p->getId()));
						uint32_t cycles = SysTimer::cycleCnt32();
						p->update();
						cycles = (SysTimer::cycleCnt32() - cycles) >> 6;
						p->cpu_cycles = cycles;
						if (cycles > p->cpu_cycles_max) p->cpu_cycles_max = cycles;
						sysCrashReport.setBreadcrumb(SysCrashReport::AUDIO_EFFECT_UPDATE_ID, SysCrashReport::DONE_MASK, (uint32_t)(p->getId()));
					}
				}
			}
		}
		//if (loopLimit == 0) { if (Serial) { Serial.printf("software_isr(): loop limit reached\n"); } }
		//Serial.printf("Loop limit %d\n", loopLimit);

		// reset and release step_update input buffers
		if (AudioStream::step_update_object) { AudioStream::step_update_object->updateIndex(-1); }
		audioIsrInProgress = false;

	} else {  // original processing by PJRC
		for (p = AudioStream::first_update; p; p = p->next_update) {
			//Serial.printf("addr:%08X\n", p);
			if (p->active) {
				uint32_t cycles = SysTimer::cycleCnt32();
				p->update();
				// TODO: traverse inputQueueArray and release
				// any input blocks that weren't consumed?
				cycles = (SysTimer::cycleCnt32() - cycles) >> 6;
				p->cpu_cycles = cycles;
				if (cycles > p->cpu_cycles_max) p->cpu_cycles_max = cycles;
			}
		}
	}

	totalcycles = (SysTimer::cycleCnt32() - totalcycles) >> 6;
	AudioStream::cpu_cycles_total = totalcycles;
	if (totalcycles > AudioStream::cpu_cycles_total_max)
		AudioStream::cpu_cycles_total_max = totalcycles;

	SysCpuControl::SysDataSyncBarrier();

	if (SysPlatform::sysWatchdog.isStarted()) {
		SysPlatform::sysWatchdog.feed();
	}

	asm("DSB");
}

