#include "enclave_management.h"
#include "server.h"

/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    /* Initialize the enclave */
    if(initialize_enclave() < 0){
        printf("Enter a character before exit ...\n");
        getchar();
        return -1; 
    }

    init_server();

    // main loop
    run_server();

    /* Destroy the enclave */
    destroy_enlave();
    
    return 0;
}

