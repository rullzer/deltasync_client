#ifndef UPLOAD_H
#define UPLOAD_H

#include <stdlib.h>
#include <string>


size_t writeHash(void *ptr, size_t size, size_t nmemb, void *stream);

using namespace std;

class upload {

public:
	upload(const char *host, const char *user, const char *pass, const char *path) {
		_host = host;
		_user = user;
		_pass = pass;
		_path = path;
	};

	void start(size_t size);
	void move(size_t from, size_t to, size_t size);
	void add(size_t start, size_t size, const char *data);
	char * done();

private:
	const char *_host;
	const char *_user;
	const char *_pass;
	const char *_path;
};

#endif
