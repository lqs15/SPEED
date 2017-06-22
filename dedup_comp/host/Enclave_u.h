#ifndef ENCLAVE_U_H__
#define ENCLAVE_U_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <string.h>
#include "sgx_edger8r.h" /* for sgx_satus_t etc. */


#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_print_string, (const char* str));
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_load_text_file, (const char* filename, char* buffer, int buffer_size, int* filesize));
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_get_time, (long int* second, long int* nanosecond));
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_request_find, (const uint8_t* tag, int* resp_size, uint8_t* rlt, int exp_size, uint8_t* meta));
void SGX_UBRIDGE(SGX_NOCONVENTION, ocall_request_put, (const uint8_t* tag, const uint8_t* rlt, int rlt_size, const uint8_t* meta));

sgx_status_t ecall_entrance(sgx_enclave_id_t eid);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif