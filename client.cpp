//
//  Weather update client in C++
//  Connects SUB socket to tcp://localhost:5556
//  Collects weather updates and finds avg temp in zipcode
//
//  Olivier Chamoux <olivier.chamoux@fr.thalesgroup.com>
//
#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>
#include <deque>
#include <vector>
#include <algorithm>
#include <thread> 
#include <mutex>
#include <condition_variable>

const size_t ms(50);
//44100 / 20 = 2205
const size_t size(44100*4*ms/1000);  


struct Chunk
{
	int32_t tv_sec;
	int32_t tv_usec;
	char payload[size];
};

std::deque<Chunk*> chunks;
std::deque<int> timeDiffs;
std::mutex mtx;
std::mutex mutex;
std::condition_variable cv;

std::string timeToStr(const timeval& timestamp)
{
	char tmbuf[64], buf[64];
	struct tm *nowtm;
	time_t nowtime;
	nowtime = timestamp.tv_sec;
	nowtm = localtime(&nowtime);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06d", tmbuf, (int)timestamp.tv_usec);
	return buf;
}


std::string chunkTime(const Chunk& chunk)
{
	timeval ts;
	ts.tv_sec = chunk.tv_sec;
	ts.tv_usec = chunk.tv_usec;
	return timeToStr(ts);
}


int diff_ms(const timeval& t1, const timeval& t2)
{
    return (((t1.tv_sec - t2.tv_sec) * 1000000) + 
            (t1.tv_usec - t2.tv_usec))/1000;
}


int getAge(const Chunk& chunk)
{
	timeval now;
	gettimeofday(&now, NULL);
	timeval ts;
	ts.tv_sec = chunk.tv_sec;
	ts.tv_usec = chunk.tv_usec;
	return diff_ms(now, ts);
}


void player() 
{
	std::unique_lock<std::mutex> lck(mtx);
	bool playing = true;
	while (1)
	{
		if (chunks.empty())
			cv.wait(lck);
		mutex.lock();
		std::cerr << "Chunks: " << chunks.size() << "\n";
		Chunk* chunk = chunks.front();
		chunks.pop_front();
		mutex.unlock();
	
//		playing = playing || (getAge(*chunks.front()) > 200);

		if (playing)
		{
			int age = getAge(*chunk) - 300;
			while (age < 0)
			{
				usleep((-age) * 1000/ 2);
				age = getAge(*chunk) - 300;
			}
			std::cerr << "Playing: " << getAge(*chunk) << "\n";
			
	        for (size_t n=0; n<size; ++n)
			{
           		std::cout << chunk->payload[n];// << std::flush;
//				if (size % 100 == 0)
//					std::cout << std::flush;
			}
			std::cout << std::flush;
		}
		delete chunk;
	}
}


int main (int argc, char *argv[])
{
    zmq::context_t context (1);
    zmq::socket_t subscriber (context, ZMQ_SUB);
    subscriber.connect("tcp://192.168.0.2:123458");

    const char* filter = "";
    subscriber.setsockopt(ZMQ_SUBSCRIBE, filter, strlen(filter));
	std::thread playerThread(player);

    while (1)
    {
        zmq::message_t update;
        subscriber.recv(&update);
		Chunk* chunk = new Chunk();
        memcpy(chunk, update.data(), sizeof(Chunk));
		timeval now;
		gettimeofday(&now, NULL);
		std::cerr << "New chunk: " << chunkTime(*chunk) << "\t" << timeToStr(now) << "\t" << getAge(*chunk) << "\n";

/*		timeDiffs.push_back(diff_ms(now, ts));
		if (timeDiffs.size() > 100)
			timeDiffs.pop_front();
		std::vector<int> v(timeDiffs.begin(), timeDiffs.end());
		std::sort(v.begin(), v.end());
		std::cerr << "Median: " << v[v.size()/2] << "\n";
*/

		mutex.lock();
		chunks.push_back(chunk);
		mutex.unlock();
		cv.notify_all();
    }
    return 0;
}

