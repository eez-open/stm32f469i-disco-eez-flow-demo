    /*
* EEZ Generic Firmware
* Copyright (C) 2020-present, Envox d.o.o.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h> // snprintf

#include <eez/core/hmi.h>
#include <eez/core/os.h>

#include <eez/flow/flow.h>

#include <eez/gui/touch.h>

#include "tasks.h"
#include "firmware.h"

#if defined(EEZ_PLATFORM_STM32)
#include "adc.h"
#endif

using namespace eez;

namespace eez {

////////////////////////////////////////////////////////////////////////////////

void highPriorityThreadMainLoop(void *);

EEZ_THREAD_DECLARE(highPriority, AboveNormal, 4 * 1024);

#if defined(EEZ_PLATFORM_STM32)
#define HIGH_PRIORITY_QUEUE_SIZE 50
#endif

#if defined(EEZ_PLATFORM_SIMULATOR)
#define HIGH_PRIORITY_QUEUE_SIZE 100
#endif

EEZ_MESSAGE_QUEUE_DECLARE(highPriority, {
	HighPriorityThreadMessage type;
	uint32_t param;
});


void initHighPriorityMessageQueue() {
	EEZ_MESSAGE_QUEUE_CREATE(highPriority, HIGH_PRIORITY_QUEUE_SIZE);
}

void startHighPriorityThread() {
	EEZ_THREAD_CREATE(highPriority, highPriorityThreadMainLoop);
}

void highPriorityThreadOneIter();

void highPriorityThreadMainLoop(void *) {
#ifdef __EMSCRIPTEN__
    highPriorityThreadOneIter();
#else
    g_highPriorityTaskHandle = osThreadGetId();

    while (1) {
        highPriorityThreadOneIter();
    }
#endif
}

void highPriorityThreadOneIter() {
    highPriorityMessageQueueObject obj;
	if (EEZ_MESSAGE_QUEUE_GET(highPriority, obj, 1)) {
        auto type = obj.type;

        if (type == HIGH_PRIORITY_THREAD_MESSAGE_DUMMY) {
        }
	}

#if defined(EEZ_PLATFORM_STM32)
    // call every 10 ms
    static int counter = 0;
    if (++counter == 10) {
        gui::touch::tick();
        counter = 0;
    }
#endif
}

bool isHighPriorityThread() {
    return osThreadGetId() == g_highPriorityTaskHandle;
}

void sendMessageToHighPriorityThread(HighPriorityThreadMessage messageType, uint32_t messageParam, uint32_t timeoutMillisec) {
    if (!g_highPriorityMessageQueueId) {
        return;
    }

    highPriorityMessageQueueObject obj;
    obj.type = messageType;
    obj.param = messageParam;
	EEZ_MESSAGE_QUEUE_PUT(highPriority, obj, timeoutMillisec);
}

////////////////////////////////////////////////////////////////////////////////

void lowPriorityThreadMainLoop(void *);

EEZ_THREAD_DECLARE(lowPriority, Normal, 24 * 1024);

EEZ_MESSAGE_QUEUE_DECLARE(lowPriority, {
	LowPriorityThreadMessage type;
	uint32_t param;
});

#define LOW_PRIORITY_THREAD_QUEUE_SIZE 10

void initLowPriorityMessageQueue() {
	EEZ_MESSAGE_QUEUE_CREATE(lowPriority, LOW_PRIORITY_THREAD_QUEUE_SIZE);
}

void startLowPriorityThread() {
	EEZ_THREAD_CREATE(lowPriority, lowPriorityThreadMainLoop);
}

void lowPriorityThreadOneIter();

void lowPriorityThreadMainLoop(void *) {
#ifdef __EMSCRIPTEN__
    lowPriorityThreadOneIter();
#else
    g_lowPriorityTaskHandle = osThreadGetId();

    while (1) {
    	lowPriorityThreadOneIter();
    }

    while (true) {
    	osDelay(1);
    }
#endif
}

void lowPriorityThreadOneIter() {
    static const uint32_t INTERVAL = 25;
    static uint32_t g_lastTickCountMs;

    lowPriorityMessageQueueObject obj;
	if (EEZ_MESSAGE_QUEUE_GET(lowPriority, obj, INTERVAL)) {
        auto type = obj.type;

		if (type == LOW_PRIORITY_THREAD_MESSAGE_DUMMY) {
        }

        uint32_t diffMs = millis() - g_lastTickCountMs;
        if (diffMs < INTERVAL) {
            return;
        }
    }

    g_lastTickCountMs = millis();

    hmi::tick();

#if defined(EEZ_PLATFORM_STM32)
    static uint32_t g_lastTickCountMsTempRead;

    if (g_lastTickCountMs - g_lastTickCountMsTempRead > 1000) {
        g_lastTickCountMsTempRead = g_lastTickCountMs;

        __HAL_ADC_ENABLE(&hadc1);
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK) {
            const uint16_t* const ADC_TEMP_3V3_30C =  reinterpret_cast<uint16_t*>(0x1FFF7A2C);
            const uint16_t* const ADC_TEMP_3V3_110C =  reinterpret_cast<uint16_t*>(0x1FFF7A2E);
            const float CALIBRATION_REFERENCE_VOLTAGE = 3.3F;

            const float REFERENCE_VOLTAGE = 3.0F; // supplied with Vref+ or VDDA

            // scale constants to current reference voltage
            float adcCalTemp30C = static_cast<float>(*ADC_TEMP_3V3_30C) * (REFERENCE_VOLTAGE/CALIBRATION_REFERENCE_VOLTAGE);
            float adcCalTemp110C = static_cast<float>(*ADC_TEMP_3V3_110C) * (REFERENCE_VOLTAGE/CALIBRATION_REFERENCE_VOLTAGE);

            uint16_t adcTempValue = HAL_ADC_GetValue(&hadc1);

            g_temperature = (static_cast<float>(adcTempValue) - adcCalTemp30C)/(adcCalTemp110C - adcCalTemp30C) * (110.0F - 30.0F) + 30.0F;        

            g_temperature = roundf(g_temperature);

            HAL_ADC_Stop(&hadc1);
        }
    }
#endif

	return;
}

bool isLowPriorityThread() {
    return osThreadGetId() == g_lowPriorityTaskHandle;
}

void sendMessageToLowPriorityThread(LowPriorityThreadMessage messageType, uint32_t messageParam, uint32_t timeoutMillisec) {
    if (!g_lowPriorityMessageQueueId) {
        return;
    }

    lowPriorityMessageQueueObject obj;
    obj.type = messageType;
    obj.param = messageParam;
	EEZ_MESSAGE_QUEUE_PUT(lowPriority, obj, timeoutMillisec);
}

} // namespace eez
