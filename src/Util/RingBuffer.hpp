/*
 * RingBuffer.h
 *
 *  Created on: 2016年8月10日
 *      Author: xzl
 */

#ifndef UTIL_RINGBUFFER_HPP_
#define UTIL_RINGBUFFER_HPP_
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
using namespace std;

namespace ZL {
namespace Util {

//实现了一个一写多读得环形缓冲的模板类
template<typename T> class RingBuffer: public enable_shared_from_this<
		RingBuffer<T> > {
public:
	typedef std::shared_ptr<RingBuffer> Ptr;

	class RingReader {
	public:
		friend class RingBuffer;
		typedef std::shared_ptr<RingReader> Ptr;
		RingReader(const std::shared_ptr<RingBuffer> &_buffer) {
			buffer = _buffer;
			curpos = _buffer->ringKeyPos;
		}
		virtual ~RingReader() {
			auto strongBuffer = buffer.lock();
			if (strongBuffer) {
				strongBuffer->release(this);
			}
		}
		//重新定位读取器至最新的数据位置
		void reset() {
			auto strongBuffer = buffer.lock();
			if (strongBuffer) {
				curpos = strongBuffer->ringPos; //定位读取器
			}
		}
		void setReadCB(const function<void(const T &)> &cb) {
			lock_guard<recursive_mutex> lck(mtxCB);
			if (!cb) {
				readCB = [](const T &) {};
			} else {
				readCB = cb;
			}
		}
		void setDetachCB(const function<void()> &cb) {
			lock_guard<recursive_mutex> lck(mtxCB);
			if (!cb) {
				detachCB = []() {};
			} else {
				detachCB = cb;
			}
		}
        const T *read(){
            auto strongBuffer=buffer.lock();
            if(!strongBuffer){
                return nullptr;
            }
            return read(strongBuffer.get());
        }
	private:
		void onRead(const T &data) const {
			decltype(readCB) cb;
			{
				lock_guard<recursive_mutex> lck(mtxCB);
				cb = readCB;
			}
			cb(data);
		}
		void onDetach() const {
			decltype(detachCB) cb;
			{
				lock_guard<recursive_mutex> lck(mtxCB);
				cb = detachCB;
			}
			cb();
		}
		//读环形缓冲
		const T *read(RingBuffer *ring) {
			if (curpos == ring->ringPos) {
				return nullptr;
			}
			const T *data = &(ring->dataRing[curpos]); //返回包
			curpos = ring->next(curpos); //更新位置
			return data;
		}
        
		function<void(const T &)> readCB = [](const T &) {};
		function<void(void)> detachCB = []() {};
		weak_ptr<RingBuffer> buffer;
		mutable recursive_mutex mtxCB;
		int curpos;
	};

	friend class RingReader;
	RingBuffer(int _ringSize = 32) {
		ringSize = _ringSize;
		dataRing = new T[ringSize];
		ringPos = 0;
		ringKeyPos = 0;
	}
	virtual ~RingBuffer() {
		decltype(readerMap) mapCopy;
		{
			lock_guard<recursive_mutex> lck(mtx_reader);
			mapCopy.swap(readerMap);
		}
		for (auto &pr : mapCopy) {
			auto reader = pr.second.lock();
			if(reader){
				reader->onDetach();
			}
		}
		delete[] dataRing;
	}

	std::shared_ptr<RingReader> attach() {
		std::shared_ptr<RingReader> ptr(new RingReader(this->shared_from_this()));
		std::weak_ptr<RingReader> weakPtr = ptr;
		lock_guard<recursive_mutex> lck(mtx_reader);
		readerMap.emplace(ptr.get(),weakPtr);
		return ptr;
	}
	//写环形缓冲，非线程安全的
	void write(const T &in,bool isKey=true) {
        dataRing[ringPos] = in;
        if (isKey) {
            ringKeyPos = ringPos; //设置读取器可以定位的点
        }
        ringPos = next(ringPos);
        decltype(readerMap) mapCopy;
        {
        	lock_guard<recursive_mutex> lck(mtx_reader);
        	mapCopy = readerMap;
        }
		for (auto &pr : mapCopy) {
			auto reader = pr.second.lock();
			if(reader){
				reader->onRead(in);
			}
		}
	}
	int readerCount(){
		lock_guard<recursive_mutex> lck(mtx_reader);
		return readerMap.size();
	}
private:
	T *dataRing;
	long ringPos;
	long ringKeyPos;
	int ringSize;
	recursive_mutex mtx_reader;
	unordered_map<void *,std::weak_ptr<RingReader> > readerMap;

	inline long next(long pos) {
		//读取器下一位置
		if (pos > ringSize - 2) {
			return 0;
		} else {
			return pos + 1;
		}
	}

	void release(RingReader *reader) {
		lock_guard<recursive_mutex> lck(mtx_reader);
		readerMap.erase(reader);
	}
};

} /* namespace Util */
} /* namespace ZL */

#endif /* UTIL_RINGBUFFER_HPP_ */
