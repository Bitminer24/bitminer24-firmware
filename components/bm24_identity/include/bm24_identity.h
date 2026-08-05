#ifndef BM24_IDENTITY_H
#define BM24_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM24_DEVICE_ID_LENGTH 36u

/* Formatiert 16 Bytes als RFC-4122-UUID. Die Versions-/Variantenbits werden
   dabei nicht veraendert; das macht der Erzeuger vor dem Speichern. */
bool bm24_identity_format_uuid(const uint8_t bytes[16], char *out,
                               size_t capacity);

/* Liefert eine beim ersten Start zufaellig erzeugte UUIDv4 aus einem eigenen
   NVS-Namensraum. WLAN-, DHCP- und Konfigurationswechsel aendern sie nicht. */
bool bm24_identity_get(char out[BM24_DEVICE_ID_LENGTH + 1]);

#ifdef __cplusplus
}
#endif

#endif /* BM24_IDENTITY_H */
