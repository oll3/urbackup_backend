#include "InternetServicePipe.h"
#include "../cryptoplugin/ICryptoFactory.h"

#include "../Interface/Server.h"
#include <string.h>

extern ICryptoFactory *crypto_fak;

InternetServicePipe::InternetServicePipe(IPipe *cs, const std::string &key)
	: cs(cs), destroy_cs(false)
{
	enc=crypto_fak->createAESEncryption(key);
	dec=crypto_fak->createAESDecryption(key);
}

InternetServicePipe::~InternetServicePipe(void)
{
	enc->Remove();
	dec->Remove();
	if(destroy_cs)
	{
		Server->destroy(cs);
	}
}

size_t InternetServicePipe::Read(char *buffer, size_t bsize, int timeoutms)
{
	size_t rc=cs->Read(buffer, bsize, timeoutms);
	if(rc>0)
	{
		size_t off=dec->decrypt(buffer, rc);
		if(off!=0 )
		{
			if(rc-off>0)
			{
				memmove(buffer, buffer+off, rc-off);
			}
			return rc-off;
		}
		return rc;
	}
	return 0;
}

std::string InternetServicePipe::decrypt(const std::string &data)
{
	return dec->decrypt(data);
}

std::string InternetServicePipe::encrypt(const std::string &data)
{
	return enc->encrypt(data);
}

bool InternetServicePipe::Write(const char *buffer, size_t bsize, int timeoutms)
{
	std::string encbuf=enc->encrypt(buffer, bsize);
	bool b=cs->Write(encbuf, timeoutms);
	return b;
}

size_t InternetServicePipe::Read(std::string *ret, int timeoutms)
{
	size_t rc=cs->Read(ret, timeoutms);
	if(rc>0)
	{
		size_t off=dec->decrypt((char*)ret->c_str(), ret->size());
		if(off!=0 )
		{
			if(rc-off>0)
			{
				memmove((char*)ret->c_str(), ret->c_str()+off, rc-off);
				ret->resize(rc-off);
			}
			else
			{
				ret->clear();
			}
			return rc-off;
		}
		return rc;
	}
	return 0;
}

bool InternetServicePipe::Write(const std::string &str, int timeoutms)
{
	return Write(str.c_str(), str.size(), timeoutms);
}

/**
* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
*/
bool InternetServicePipe::isWritable(int timeoutms)
{
	return cs->isWritable(timeoutms);
}

bool InternetServicePipe::isReadable(int timeoutms)
{
	return cs->isReadable(timeoutms);
}

bool InternetServicePipe::hasError(void)
{
	return cs->hasError();
}

void InternetServicePipe::shutdown(void)
{
	cs->shutdown();
}

size_t InternetServicePipe::getNumElements(void)
{
	return cs->getNumElements();
}

IPipe *InternetServicePipe::getRealPipe(void)
{
	return cs;
}

void InternetServicePipe::destroyBackendPipeOnDelete(bool b)
{
	destroy_cs=b;
}

void InternetServicePipe::setBackendPipe(IPipe *pCS)
{
	cs=pCS;
}

void InternetServicePipe::addThrottler(IPipeThrottler *throttler)
{
	cs->addThrottler(throttler);
}

void InternetServicePipe::addOutgoingThrottler(IPipeThrottler *throttler)
{
	cs->addOutgoingThrottler(throttler);
}

void InternetServicePipe::addIncomingThrottler(IPipeThrottler *throttler)
{
	cs->addIncomingThrottler(throttler);
}

_i64 InternetServicePipe::getTransferedBytes(void)
{
	return cs->getTransferedBytes();
}

void InternetServicePipe::resetTransferedBytes(void)
{
	cs->resetTransferedBytes();
}