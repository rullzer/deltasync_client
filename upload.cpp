#include "upload.h"
#include <sys/types.h>


#include <curl/curl.h>
#include <string>
#include <string.h>


char *__hash;

using namespace std;

void upload::start(size_t size) {
	CURL *h = curl_easy_init();

	string url = _host;
	url = url + "/index.php/apps/deltasync/api/0.0.1/upload/start/" + _path;

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERNAME, _user);
	curl_easy_setopt(h, CURLOPT_PASSWORD, _pass);
	curl_easy_setopt(h, CURLOPT_POST, 1);

	string data = "size=" + to_string(size);
	curl_easy_setopt(h, CURLOPT_POSTFIELDS, data.c_str());

	CURLcode res = curl_easy_perform(h);

	if (res != CURLE_OK) {
		printf("ERROR\n");
	}
	printf("\n\nStarted delta sync\n");

	curl_easy_cleanup(h);
}

void upload::move(size_t from, size_t to, size_t size) {
	CURL *h = curl_easy_init();

	string url = _host;
	url = url + "/index.php/apps/deltasync/api/0.0.1/upload/move/" + _path;

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERNAME, _user);
	curl_easy_setopt(h, CURLOPT_PASSWORD, _pass);
	curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "PATCH");

	string data = "from=" + to_string(from) + "&to=" + to_string(to) + "&size=" + to_string(size);;
	curl_easy_setopt(h, CURLOPT_POSTFIELDS, data.c_str());

	CURLcode res = curl_easy_perform(h);

	if (res != CURLE_OK) {
		printf("ERROR\n");
	}
	printf("Moved %lu bytes at %lu to %lu\n", size, from, to);

	curl_easy_cleanup(h);
}

void upload::add(size_t start, size_t size, const char *data) {
	CURL *h = curl_easy_init();

	string url = _host;
	url = url + "/index.php/apps/deltasync/api/0.0.1/upload/add/" + _path;

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERNAME, _user);
	curl_easy_setopt(h, CURLOPT_PASSWORD, _pass);
	curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "PATCH");

	char *data2 = curl_easy_escape(h, data, size);

	string pdata = "start=" + to_string(start) + "&size=" + to_string(size) + "&data=" + data2;
	curl_easy_setopt(h, CURLOPT_POSTFIELDS, pdata.c_str());

	CURLcode res = curl_easy_perform(h);

	if (res != CURLE_OK) {
		printf("ERROR\n");
	}
	printf("Added %lu bytes at %lu\n", size, start);

	curl_easy_cleanup(h);
}

char * upload::done() {

	CURL *h = curl_easy_init();

	string url = _host;
	url = url + "/index.php/apps/deltasync/api/0.0.1/upload/done/" + _path;

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERNAME, _user);
	curl_easy_setopt(h, CURLOPT_PASSWORD, _pass);
	curl_easy_setopt(h, CURLOPT_POST, 1);
	curl_easy_setopt(h, CURLOPT_POSTFIELDS, "");

	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeHash);

	CURLcode res = curl_easy_perform(h);

	if (res != CURLE_OK) {
		printf("ERROR\n");
	}
	curl_easy_cleanup(h);
	return __hash;
}

size_t writeHash(void *ptr, size_t size, size_t nmemb, void *stream) {
	__hash = (char *)malloc(sizeof(char) * 100);

	strncpy(__hash, (char *)ptr, size*nmemb);

	return size*nmemb;
}

