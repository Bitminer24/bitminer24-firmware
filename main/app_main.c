/* BitMiner24 Firmware 2.0 — Phase-1-Skelett.
   Beweist: ESP-IDF 5 bootet auf dem T-Display S3, die Modulstruktur baut,
   und ein Kern-Helfer aus 1.x laeuft hier identisch (dieselbe Logik ist
   host-getestet in test/test_format). Mining folgt in Phase 2. */

#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "bm24_format.h"

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n=== BitMiner24 Firmware 2.0 (Phase 1) ===\n");
    printf("ESP-IDF %s, %d Kerne, Rev. %d\n",
           esp_get_idf_version(), chip.cores, chip.revision);

    char buf[32];
    printf("Formatprobe: %s EUR\n", bm24_thousands(179009, buf, sizeof(buf)));

    uint32_t beat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        printf("[BM24v2] Herzschlag %" PRIu32 ", freier Heap %" PRIu32 " Byte\n",
               ++beat, (uint32_t)esp_get_free_heap_size());
    }
}
