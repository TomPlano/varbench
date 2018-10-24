/*
 * This file is part of the 'varbench' project developed by the
 * University of Pittsburgh with funding from the United States
 * National Science Foundation and the Department of Energy.
 *
 * Copyright (c) 2017, Brian Kocoloski <briankoco@cs.pitt.edu>
 *
 * This is free software.  You are permitted to use, redistribute, and
 * modify it as specified in the file "LICENSE.md".
 */



#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <error.h>

#include <varbench.h>
#include <socketcorpus.h> 

#include <cJSON.h>

//define our socket lib function pointers
typedef int (*socket_lib_fun)(vb_socketcall_info_t* info);

//common calls to start and stop
socket_lib_fun init_socketcall_info_t;
socket_lib_fun deinit_socketcall_info_t;

static int
write_syscall_times(int rank, time_data_t * call_seq,  int call_seq_len)
{
    int status;
    int i;
    FILE* outfile;
    mkdir("./test/", 0777);
    outfile = fopen("./test/test.csv", "a+");
    if (outfile==NULL){
        perror("Error: ");
        return -1;
    }

    for (i=0;i<call_seq_len;i++){
        fprintf(outfile,"%i, %i, %lu, %lu, %lu\n", rank, call_seq[i].syscall_num,call_seq[i].time_in, call_seq[i].time_out,  call_seq[i].time_out-call_seq[i].time_in);
    }
    
    fclose(outfile);

    return 0;
}

static int
iteration(vb_instance_t    * instance,
            void           * lib_handle,
            vb_socketcall_info_t* info)
{
	//work
    int status, i;
    socket_lib_fun fn;
    
    //launch untimed portion of code 
    fn = (socket_lib_fun)dlsym(lib_handle,"begin_untimed_program");
    info->timer_choice = TIME_CLIENT;
    status  = fn(info);
    vb_print_root("staring untimed task: %i\n", status);

    for (i = 0; i < info->program_info->list_length; i++) {
    fn = (socket_lib_fun)dlsym(lib_handle,info->program_info->program_list[i]);
    status  = fn(info);
    if (info->connection_failed)
    {
        vb_print_root("Connection failed, aborting itteration\n");
        break;

    }
    vb_print_root("status of %s: %i\n",info->program_info->program_list[i], status);
    }

	return 0;
}

static int
get_json_object(vb_instance_t * instance,
                char          * json_file,
                cJSON        ** root)
{
    off_t off_len;
    int   len;
    char * file_contents;

    /* Root gets the json */
    if (instance->rank_info.global_id == 0) {
        *root = cJSON_GetObjectFromFile(json_file, (void **)&file_contents, &off_len);
        len = (int)off_len;
    }
        /* Broadcast size */
    MPI_Bcast(
        &len,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    /* check error */
    if (len == -1)
        return VB_GENERIC_ERROR;

    /* Non root specific */
    if (instance->rank_info.global_id != 0) {
        file_contents = malloc(sizeof(char) * len);
        if (file_contents == NULL) {
            vb_error("Could not allocate space for JSON object (%s)\n", strerror(errno));
            return VB_GENERIC_ERROR;
        }
    }

    /* Broadcast data */
    MPI_Bcast(
        file_contents,
        len,
        MPI_CHAR,
        0,
        MPI_COMM_WORLD
    );

    /* You gotta save it somewhere dude ...*/
    if (instance->rank_info.global_id != 0) {
        *root = cJSON_Parse(file_contents);
    }

    /* Teardown */
    if (instance->rank_info.global_id == 0) {
        munmap(file_contents, len);
    } else {
        free(file_contents);
    }

    return VB_SUCCESS;

}
static void 
clean_up_socket_program_t_list(socket_program_t* sock_prog)
{
    int i;
    for(i = 0; i<sock_prog->list_length; i++){
        free(sock_prog->program_list[i]);
    }
    free(sock_prog->program_list);
    free(sock_prog);
}

static int 
build_socket_program_t_list(cJSON* socket_config,
                          cJSON* syscall_list,
                          socket_program_t* sock_prog)
{
    int i;

    sock_prog->connection_domain =cJSON_GetObjectItemCaseSensitive(socket_config, "connection_domain")->valueint;
    sock_prog->connection_type =cJSON_GetObjectItemCaseSensitive(socket_config, "connection_type")->valueint;
    sock_prog->connection_proto=cJSON_GetObjectItemCaseSensitive(socket_config, "connection_proto")->valueint;
    sock_prog->connection_port=cJSON_GetObjectItemCaseSensitive(socket_config, "connection_port")->valueint;
    //prog list stuff
    sock_prog->list_length = cJSON_GetArraySize(syscall_list);

    sock_prog->program_list = (char**)malloc(sock_prog->list_length*sizeof(char*));
    if (!sock_prog->program_list) {
            vb_error("Out of memory\n");
            free(sock_prog);
            return VB_GENERIC_ERROR;
        }

    for (i = 0; i < sock_prog->list_length; i++) {
        sock_prog->program_list[i] = (char*)malloc(MAXNAMESIZE*sizeof(char));
        snprintf(sock_prog->program_list[i], MAXNAMESIZE, "%s", cJSON_GetArrayItem(syscall_list, i)->valuestring);
    }

    return VB_SUCCESS;
}




static int
run_kernel(vb_instance_t    * instance,
           unsigned long long iterations,
           char* json_file)
{
	int iter, dummy,fd, status;
	struct timeval t1, t2;
    void* lib_handle;
    cJSON *json_obj, *socket_config, *syscall_list;
    vb_socketcall_info_t info;
    socket_program_t* iter_independent_config;
    //get dll for socket corpus and save off init and deinit functions
    lib_handle = dlopen("/home/planot/varbench/src/kernels/corpuses/socket-corpus/socketcorpus.so", RTLD_NOW|RTLD_GLOBAL);
    if (lib_handle == NULL) {
        vb_error("dlopen failed\n");
        return VB_GENERIC_ERROR;
    }

    init_socketcall_info_t = (socket_lib_fun)dlsym(lib_handle,"init_vb_socketcall_info_t");
    deinit_socketcall_info_t = (socket_lib_fun)dlsym(lib_handle,"deinit_vb_socketcall_info_t");

    
    /* Get JSON object describing program */
    status = get_json_object(instance, json_file, &json_obj);
    if (status != VB_SUCCESS) {
        vb_error("Unable to build JSON object\n");
        return status;
    }

    socket_config = cJSON_GetObjectItemCaseSensitive(json_obj, "socket_config");
    if (!cJSON_IsObject(socket_config)) {
        vb_error("Unable to retrieve 'socket_config' object from JSON object\n");
        goto out_json;
    }

    syscall_list = cJSON_GetObjectItemCaseSensitive(json_obj, "syscall_list");
    if (!cJSON_IsArray(syscall_list)) {
        vb_error("Unable to retrieve 'syscall_list' object from JSON object\n");
        status = VB_GENERIC_ERROR;
        goto out_json;
    }

    
    //Build a thing that my itteration fun can parse
    iter_independent_config= (socket_program_t*)malloc(sizeof(socket_program_t));
    status = build_socket_program_t_list(socket_config, syscall_list, iter_independent_config);
    if (status != VB_SUCCESS) {
        vb_error("Unable to build program list\n");
        goto out_json;
    }

    //increase base port by rank
    iter_independent_config->connection_port += instance->rank_info.local_id;
    info.program_info=iter_independent_config;

	for (iter = 0; iter < iterations; iter++) {
        init_socketcall_info_t(&info);
		MPI_Barrier(MPI_COMM_WORLD);
        gettimeofday(&t1, NULL);
		dummy = iteration(instance,lib_handle, &info);
        gettimeofday(&t2, NULL);
        MPI_Barrier(MPI_COMM_WORLD);

        //write out each syscalls time to csv
        if(!info.connection_failed)
        {
            status = write_syscall_times(instance->rank_info.local_id, info.call_seq,  info.call_seq_len);
            if(status!=0)
            {
                vb_error("syscall csv write failed\n");
                return VB_GENERIC_ERROR;
            }
        }
        //tear down env
        deinit_socketcall_info_t(&info);


        status = vb_gather_kernel_results(
                instance,
                iter,
                &t1,
                &t2
            );

		/* Prevent compiler optimization by writing the dummy val somewhere */
        fd = open("/dev/null", O_RDWR);
        assert(fd > 0);
        write(fd, &dummy, sizeof(int));
        close(fd);

        if (status != VB_SUCCESS) {
            vb_error("Could not gather kernel results\n");
            return status;
        }
       

        
        
        vb_print_root("itteration %i finished\n", iter);
    }

    status = dlclose(lib_handle);
    if(status!=0)
    {
        vb_error("dlclose failed");
        return VB_GENERIC_ERROR;
    }

    //if i print to a file after the dlclose then all is well


    clean_up_socket_program_t_list(iter_independent_config);


out_json:
    cJSON_Delete(json_obj);

    return status;
}

static void
usage(void){

	vb_error_root("\nsocket kernel required args:\n"
		"  arg 0: <path to corpus json file>\n"
		);
}

int 
vb_kernel_socket(int             argc,
                  char         ** argv,
                  vb_instance_t * instance)
{
    char * json_file;
	if(argc!=1){
		usage();
		return VB_BAD_ARGS;
	}
    json_file = argv[0];

    srand(time(0)); 
    return run_kernel(instance,
    	instance->options.num_iterations,
        json_file);
}
