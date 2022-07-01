// gcc -Os *.c -lxml2 -luv -lpcre -lcurl -o spider
// time clang++ -std=c++11 *.cpp -I/opt/homebrew/include -L/opt/homebrew/lib -lxml2 -lcurl -g

#include "spider.h"

int main()
{
	char agent[] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:42.0) "
				   "Gecko/20100101 Firefox/42.0";
	setlocale(LC_ALL, "");

	Timer timer;

	Spider spider;

	spider.setUrl("http://www.meitulu.cn");
	spider.setBaseUrl("www.meitulu.cn");

	spider.setUserAgent(agent);

	return spider.RunLoop();
}
