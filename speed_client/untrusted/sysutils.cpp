//#include "sgx_urts.h"
#include "Enclave_u.h"
#include <stdio.h>
#include <time.h>
#include "../../common/config.h"
#include "../../common/data_type.h"
#include "../../common/network/network.h"
#include <assert.h>
#include <fstream>
#include <sstream>


#include "pgm_tool.h"

void ocall_print_string(const char *str)
{
    /* Proxy/Bridge will check the length and null-terminate
    * the input string to prevent buffer overflow.
    */
    printf("%s", str);
}

void ocall_get_time(int *second, int *nanosecond)
{
    timespec wall_clock;
    clock_gettime(CLOCK_REALTIME, &wall_clock);
    *second = wall_clock.tv_sec;
    *nanosecond = wall_clock.tv_nsec;
}


void ocall_load_text_file(const char *filename,char **pbuffer, int buffer_size,int *filesize) 
{
    // consider alternative impl to speed up loading of large file
	int realsize = ocall_file_size(filename);
	*pbuffer = new char[realsize];
	memset(*pbuffer, 0 , realsize);

	//printf("app new %p\n", *pbuffer);

	if (strstr(filename, ".pgm") != 0)
	{
		pgm_tool pgmTool;
		pgm_tool::PgmBuffer pgm_buffer;

		std::ifstream in(filename, std::ios::binary);

		if (in.good()) 
		{
			pgmTool.extractPgm(in, pgm_buffer);

			int dataSize = pgm_buffer.width*pgm_buffer.height * sizeof(float);
			char* databuffer = *pbuffer;

			memcpy(databuffer, &pgm_buffer.width, sizeof(int));
			databuffer += sizeof(int);
			memcpy(databuffer, &pgm_buffer.height, sizeof(int));
			databuffer += sizeof(int);
			memcpy(databuffer, pgm_buffer.data, dataSize);

			*filesize = dataSize + sizeof(int)*2;

			if (*filesize > realsize)
			{
				printf("app load pgm size error, buffer is %d, file is %d.\n", realsize, *filesize);
			}

			//printf("read pgm file, w:%d ,h:%d ,size:%d\n", pgm_buffer.width,pgm_buffer.height,dataSize);
		}
		else 
		{
			printf("[*] Fail to open file %s\n", filename);
			*filesize = 0;
		}
		
		in.close();
		return;
	}

    std::ifstream infile(filename);
    if (infile.good()) {
        std::stringstream sstream;
        sstream << infile.rdbuf();
        std::string file = sstream.str();
		if (file.size() != realsize)
		{
			printf("load text size error, file is %d, str is %d.\n", realsize, (int)file.size());
		}
        memcpy(*pbuffer, file.data(), file.size());
        *filesize = file.size();
    }
    else {
        printf("[*] Fail to open file %s\n", filename);
        *filesize = 0;
    }

    infile.close();
}

void ocall_write_text_file(const char *filename, const char *buffer, int buffer_size)
{
	std::ofstream outfile(filename, std::ios::out&std::ios::trunc);
	if (outfile.good()) {
		//outfile.write("\n\n", 2);
		outfile.write(buffer, buffer_size);
	}
	else {
		printf("[*] Fail to open file %s\n", filename);
	}

	outfile.close();
}

#include <sstream>
#include <vector>
#include <map>
#include <stdint.h>
#include <time.h>
#include <thread>
#include <mutex>
#include "../../common/config.h"

using std::mutex;
mutex cacheLock;
int network_get_time;
int network_put_time;

#ifndef USE_LOCAL_CACHE

std::auto_ptr<Network> requester;

void ocall_request_find(const uint8_t *tag,
	uint8_t *meta,
	uint8_t *rlt, int expt_size,
	int *true_size)
{
	int req_size = REQ_TYPE_SIZE + TAG_SIZE + sizeof(int);
	cacheLock.lock();

	if (req_size <= TX_BUFFER_SIZE) {
		requester->set_send_buffer(&Request::Get, REQ_TYPE_SIZE, 0);
		requester->set_send_buffer(tag, TAG_SIZE, REQ_TYPE_SIZE);
		requester->set_send_buffer((uint8_t *)(&expt_size), sizeof(int), REQ_TYPE_SIZE + TAG_SIZE);

		Request req(requester->get_send_buffer(), req_size);

		requester->send_request(&req);

		std::auto_ptr<Response> resp(requester->recv_response());

		if (resp->get_answer() == Response::Positive) {
			// copy back responsed data
			memcpy(meta, resp->get_meta(), sizeof(metadata));
			memcpy(rlt, resp->get_rlt(), resp->get_rlt_size());
			*true_size = resp->get_rlt_size();
		}
		else {
			*true_size = 0;
		}
		network_get_time += requester->last_send_request_time + requester->last_recv_response_time;
		//printf("[*] Query server to get data use %d us!\n", 
		//	requester.get()->last_send_request_time + requester.get()->last_recv_response_time
		//);
	}
	else {
		printf("[*] The get request is oversized!\n");
	}

	cacheLock.unlock();
}

void ocall_request_put(const uint8_t *tag,
	const uint8_t *meta,
	const uint8_t *rlt, int rlt_size)
{
	cacheLock.lock();
	int req_size = REQ_TYPE_SIZE + TAG_SIZE + sizeof(metadata) + rlt_size;

	if (req_size <= TX_BUFFER_SIZE) {
		requester->set_send_buffer(&Request::Put, REQ_TYPE_SIZE, 0);
		requester->set_send_buffer(tag, TAG_SIZE, REQ_TYPE_SIZE);
		requester->set_send_buffer(meta, sizeof(metadata), REQ_TYPE_SIZE + TAG_SIZE);
		requester->set_send_buffer(rlt, rlt_size, REQ_TYPE_SIZE + TAG_SIZE + sizeof(metadata));

		Request req(requester->get_send_buffer(), req_size);

		// send out request
		requester->send_request(&req);

		// no need to wait for response since the put is made asyncrounous, immediately return
		network_put_time += requester->last_send_request_time;
		//printf("[*] Query server to put data use %d us!\n",
		//	requester.get()->last_send_request_time
		//);
	}
	else {
		printf("[*] The put request is oversized!\n");
	}
	cacheLock.unlock();
};

#else

#include <redis3m/redis3m.hpp>

using namespace redis3m;
connection::ptr_t m_ptrConnection;


typedef std::vector<uint8_t> binary;
typedef std::pair<binary, binary> entry_t;
// TODO: replace with unordered_map
// We need to know map max size to use unordered_map
// typedef std::unordered_map<binary, entry_t> cache_t;
typedef std::map<binary, entry_t> cache_t;

cache_t cache;

using std::string;

static void init_local_cache()
{
	static bool done = false;
	if (!done)
	{
		const char* host = "localhost";
		const int port = 6379;
		m_ptrConnection = connection::create(host, port);

		done = true;
	}
}


static void redis_put(const string& key, const string& value)
{
	m_ptrConnection->run(command("SET") << key << value);
}

static void redis_get(const string&key, string& value)
{
	reply oReply = m_ptrConnection->run(command("GET") << key);
	value = oReply.str();
}

void ocall_request_find(const uint8_t *tag,
	uint8_t *meta,
	uint8_t *rlt, int expt_size,
	int *true_size)
{
	init_local_cache();

	string key, value;
	key.assign((char*)tag, TAG_SIZE);

	cacheLock.lock();
	redis_get(key, value);
	cacheLock.unlock();

	if (value.size() > 0)
	{
		*true_size = value.size()- sizeof(metadata);
		memcpy(meta, value.data(), sizeof(metadata));
		memcpy(rlt, value.data()+ sizeof(metadata), *true_size);
	}
	else
	{
		*true_size = 0;
	}
	//printf("find begin.\n");
	//binary key(TAG_SIZE, 0);
	//memcpy(&key[0], tag, TAG_SIZE);

	//cache_t::const_iterator it = cache.find(key);

	//if (it == cache.end())
	//{
	//	//printf("cache miss.\n");
	//	*true_size = 0;
	//}
	//else
	//{
	//	//printf("cache hit.\n");
	//	const entry_t &entry = it->second;
	//	*true_size = entry.second.size();
	//	memcpy(meta, &entry.first[0], entry.first.size());
	//	memcpy(rlt, &entry.second[0], std::min(*true_size, expt_size));
	//}
}

void ocall_request_put(const uint8_t *tag,
	const uint8_t *meta,
	const uint8_t *rlt, int rlt_size)
{
	string key;
	key.assign((char*)tag, TAG_SIZE);

	string value;
	value.assign((char*)meta, sizeof(metadata));

	value.append((char*)rlt, rlt_size);

	cacheLock.lock();
	redis_put(key, value);
	cacheLock.unlock();
	//
	//binary key(TAG_SIZE, 0);
	//memcpy(&key[0], tag, TAG_SIZE);

	//binary entry_meta(sizeof(metadata), 0);
	//memcpy(&entry_meta[0], meta, sizeof(metadata));

	//binary entry_rlt(rlt_size, 0);
	//memcpy(&entry_rlt[0], rlt, rlt_size);

	//cacheLock.lock();
	//cache[key] = entry_t(entry_meta, entry_rlt);
	//cacheLock.unlock();
}

#endif // !USE_LOCAL_CACHE

int ocall_get_network_get_time()
{
	int tmp = network_get_time;
	network_get_time = 0;
	return tmp;
}

int ocall_get_network_put_time()
{
	int tmp = network_put_time;
	network_put_time = 0;
	return tmp;
}

extern sgx_enclave_id_t global_eid;

static void call_ecall_map(int no)
{
	//std::this_thread::sleep_for(std::chrono::seconds(1));
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;
	ret = ecall_map_thread(global_eid, no);
	if (ret != SGX_SUCCESS)
		abort();
}

static void call_ecall_reduce(int no)
{
	//std::this_thread::sleep_for(std::chrono::seconds(1));
	sgx_status_t ret = SGX_ERROR_UNEXPECTED;
	ret = ecall_reduce_thread(global_eid, no);
	if (ret != SGX_SUCCESS)
		abort();
}

using std::thread;

void ocall_begin_map(int count)
{
	thread** ths = new thread*[count];
	for (int i = 0; i < count; i++)
	{
		ths[i] = new thread(call_ecall_map, i);
	}

	for (int i = 0; i < count; i++)
	{
		ths[i]->join();
		delete(ths[i]);
		ths[i] = 0;
	}
	delete[] ths;
	return;
}

void ocall_begin_reduce(int count)
{
	thread** ths = new thread*[count];
	for (int i = 0; i < count; i++)
	{
		ths[i] = new thread(call_ecall_reduce, i);
	}

	for (int i = 0; i < count; i++)
	{
		ths[i]->join();
		delete(ths[i]);
		ths[i] = 0;
	}
	delete[] ths;
	return;
}

#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>

int ocall_open(const char* filename, int mode) {

	return open(filename, (mode_t)mode);
}

int ocall_read(int file, void *buf, unsigned int size)
{
	return read(file, buf, size);
}

int ocall_write(int file, void *buf, unsigned int size)
{
	return write(file, buf, size);
}

void ocall_close(int file)
{
	close(file);
}

#include <dirent.h>  
using std::vector;
using std::string;

static void dir_oper(char const*path, vector<string>& files, bool recursion);

static bool dir_files(char const*path, vector<string>& files, bool recursion)
{
	struct stat s_buf;

	// get path info
	stat(path, &s_buf);
	// if path is dir, then open it
	if (S_ISDIR(s_buf.st_mode))
	{
		dir_oper(path, files, recursion);
		return true;
	}
	// or the path is a file, then output it 
	else if (S_ISREG(s_buf.st_mode))
	{
		return false;
	}
}

static void dir_oper(char const*path, vector<string>& files, bool recursion)
{
	// output dir paht
	struct dirent *filename;
	struct stat s_buf;

	// get DIR object
	DIR *dp = opendir(path);

	// read all files
	while (filename = readdir(dp))
	{
		// piece the full file path
		char file_path[200];
		bzero(file_path, 200);
		strcat(file_path, path);
		strcat(file_path, "/");
		strcat(file_path, filename->d_name);

		// except . and ..
		if (strcmp(filename->d_name, ".") == 0 || strcmp(filename->d_name, "..") == 0)
		{
			continue;
		}

		// use the full path to get stat
		stat(file_path, &s_buf);

		// if it is a dir, then loop self
		if (S_ISDIR(s_buf.st_mode))
		{
			if (recursion)
			{
				dir_oper(file_path, files, recursion);
			}
			//printf("\n");
		}

		// if it is a file 
		if (S_ISREG(s_buf.st_mode))
		{
			files.push_back(file_path);
			//printf("[%s] is a regular file\n", file_path);
		}
	}
}

int ocall_read_dir(const char* filename, char** buffer, int file_path_len)
{
	vector<string> child_files;
	if (dir_files(filename, child_files, true))
	{
		int file_count = child_files.size();
		if (file_count>0)
		{
			*buffer = new char[file_path_len * file_count];
			memset(*buffer, 0, file_path_len * file_count);
			
			char* temp = *buffer;
			for (int i = 0;  i < child_files.size(); i++)
			{
				strncpy(temp, child_files.at(i).c_str(), file_path_len);
				temp += file_path_len;
			}
		}
	}
	//do open ahead 
	init_local_cache();
	return child_files.size();
}

int ocall_file_size(const char* filename)
{
	//printf("Query %s size.\n", filename);
	struct stat s_buf;

	// get path info
	::stat(filename, &s_buf);

	if (strstr(filename, ".pgm") != 0)
	{
		s_buf.st_size = s_buf.st_size * 4 + 8;
	}

	//printf("%s size is %d\n", filename, s_buf.st_size);

	return s_buf.st_size;
}

void ocall_delete_array(char* pointer)
{
	if (pointer)
	{
		delete[] pointer;
	}
}

#include <pcap/pcap.h>
#include "../trusted/pktreader.h"
void ocall_load_pkt_file(const char *filename, char **buffer, int *pkt_count)
{
	pcap_t *handle;			/* Session handle */
	char errbuf[PCAP_ERRBUF_SIZE];	/* Error string */
	struct bpf_program fp;		/* The compiled filter */
	char filter_exp[] = "tcp";	/* The filter expression */
	bpf_u_int32 mask;		/* Our netmask */
	bpf_u_int32 net;		/* Our IP */
	PktReader reader;

	/* Open the session in promiscuous mode */
	handle = pcap_open_offline(filename, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open file %s: %s\n", filename, errbuf);
	}

	/* Compile and apply the filter */
	if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
		fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
	}
	if (pcap_setfilter(handle, &fp) == -1) {
		fprintf(stderr, "Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
	}

	size_t maxPktCount = (*pkt_count)<=0?6000000: (*pkt_count);
	int loadPktCount = 0;
	size_t pktTotalSize = pkt_buffer_size * maxPktCount;
	char* pktBuffer = (char*)malloc(pktTotalSize);
	char* curPktBuffer = pktBuffer;

	if (!pktBuffer)
	{
		fprintf(stderr, "Not enouth memory %s: %s\n", filter_exp, pcap_geterr(handle));
	}

	memset(pktBuffer, 0, pktTotalSize);
	//printf("max pkt_count is %d packet, size_t len is %d\n", maxPktCount, sizeof(size_t));
	//for (int i = 0; i < maxPktCount; i++)
	//{
	//	memset(curPktBuffer, 0, pkt_buffer_size);
	//	curPktBuffer += pkt_buffer_size;
	//	if (i % 10000 == 0)
	//	{
	//		printf("have clear %d packet. %d/%ld\n", i, i * pkt_buffer_size, pktTotalSize);
	//	}
	//}
	//exit(1);

	const uint8_t *packet;		/* The actual packet */
	struct pcap_pkthdr header;	/* The header that pcap gives us */
	int pageloadSize;

	while (maxPktCount > 0)
	{
		/* Grab a packet */
		packet = pcap_next(handle, &header);

		// eof
		if (!packet)
		{
			break;
		}

		// not full packet
		if (header.len > header.caplen)
		{
			continue;
		}

		// packet too big
		if (header.caplen >= pkt_buffer_size)
		{
			continue;
		}

		// tcp pageload size too big or zero
		reader.attach((const char*)packet);
		pageloadSize = reader.getPayloadLength();
		if (pageloadSize==0 ||pageloadSize > pkt_buffer_size - 54)
		{
			continue;
		}

		memcpy(curPktBuffer, packet, header.caplen);
		//memcpy(pktBuffer , packet, header.caplen);
		++loadPktCount;
		--maxPktCount;
		curPktBuffer += pkt_buffer_size;

		if (loadPktCount % 100000 == 0)
		{
			printf("have load %d packet\n", loadPktCount);
		}
	}
	/* And close the session */
	pcap_close(handle);

	*buffer = pktBuffer;
	*pkt_count = loadPktCount;


}

void ocall_free(void* pointer, int isArray)
{
	if (pointer)
	{
		if (isArray)
		{
			delete[] pointer;
		}
		else
		{
			delete pointer;
		}
	}
}

static char* nextTag(bool clear = false)
{
	static byte tag[TAG_SIZE] = { 0 };

	if (!clear)
	{
		(*(long*)tag)++;
	}
	else
	{
		memset(tag, 0, sizeof(tag));
	}

	return (char*)tag;
}


void ocall_put_get_time()
{
	hrtime start_time, end_time;
	const int dataSizes[] = { 1024 * 1, 1024 * 1024, 1024 * 100, 1024 * 10, 1024 * 1 };
	const int round = 100;
	const bool hit_cache = false;
	init_local_cache();

	for (int nn = 0; nn < sizeof(dataSizes) / sizeof(int); nn++)
	{
		printf("DataSize is %d bytes.\n", dataSizes[nn]);

		byte** tags = new byte*[round];
		metadata* metas = new metadata[round];
		byte** datas = new byte*[round];
		byte** dataBuffer = new byte*[round];

		for (int i = 0; i < round; i++)
		{
			tags[i] = new byte[TAG_SIZE];
			memcpy(tags[i], nextTag(), TAG_SIZE);

			datas[i] = new byte[dataSizes[nn]];

			dataBuffer[i] = new byte[dataSizes[nn]];
		}

		ocall_get_time(&start_time.second, &start_time.nanosecond);
		for (int i = 0; i < round; i++)
		{
			ocall_request_put(tags[i], (byte*)&metas[i], datas[i], dataSizes[nn]);
		}
		ocall_get_time(&end_time.second, &end_time.nanosecond);
		printf("In local put %d round use %d us.\n", round, time_elapsed_in_us(&start_time, &end_time));

		if (!hit_cache)
		{
			for (int i = 0; i < round; i++)
			{
				memcpy(tags[i], nextTag(), TAG_SIZE);
			}
			printf("All get request will miss cache");
		}

		int trueSize = 0;
		ocall_get_time(&start_time.second, &start_time.nanosecond);
		for (int i = 0; i < round; i++)
		{
			ocall_request_find(tags[i], (byte*)&metas[i], dataBuffer[i], dataSizes[nn], &trueSize);
			if (trueSize > 0)
			{
				memcpy(datas[i], dataBuffer[i], trueSize);
			}
		}
		ocall_get_time(&end_time.second, &end_time.nanosecond);
		printf("In local get %d round use %d us.\n\n", round, time_elapsed_in_us(&start_time, &end_time));

	}// end for loop
}