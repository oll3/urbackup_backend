/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "server_get.h"

#include "../Interface/Server.h"
#include "mbr_code.h"
#include "server_log.h"
#include "server_update_stats.h"
#include "../stringtools.h"
#include "server_cleanup.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "server_writer.h"
#include "zero_hash.h"
#include "server_running.h"
#include "../md5.h"

#include <memory.h>
#include <stdlib.h>

const unsigned int stat_update_skip=20;
const unsigned int sector_size=512;
const unsigned int sha_size=32;
const size_t minfreespace_image=1000*1024*1024; //1000 MB
const unsigned int image_timeout=10*24*60*60*1000;
const unsigned int image_recv_timeout=1*60*60*1000;
const unsigned int image_recv_timeout_after_first=10*60*1000;
const unsigned int mbr_size=(1024*1024)/2;

extern std::string server_identity;
extern std::string server_token;
extern IFSImageFactory *image_fak;

void writeZeroblockdata(void)
{
	const int64 vhd_blocksize=(1024*1024/2);
	unsigned char *zeroes=new unsigned char[vhd_blocksize];
	memset(zeroes, 0, vhd_blocksize);
	unsigned char dig[sha_size];
	sha256(zeroes, vhd_blocksize, dig);
	IFile *out=Server->openFile("zero.hash", MODE_WRITE);
	out->Write((char*)dig, sha_size);
	Server->destroy(out);
	delete []zeroes;
}

bool BackupServerGet::doImage(const std::string &pLetter, const std::wstring &pParentvhd, int incremental, int incremental_ref, bool transfer_checksum)
{
	CTCPStack tcpstack(internet_connection);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed - CONNECT error", LL_ERROR);
		return false;
	}

	std::string sletter=pLetter;
	if(pLetter!="SYSVOL")
	{
		sletter=pLetter[0];
	}

	bool with_checksum=false;
	std::string chksum_str="";
	if(transfer_checksum && image_protocol_version>0)
	{
		chksum_str="&checksum=1";
		with_checksum=true;
	}

	if(pParentvhd.empty())
	{
		tcpstack.Send(cc, server_identity+"FULL IMAGE letter="+pLetter+"&token="+server_token+chksum_str);
	}
	else
	{
		IFile *hashfile=Server->openFile(os_file_prefix(pParentvhd+L".hash"));
		if(hashfile==NULL)
		{
			ServerLogger::Log(clientid, "Error opening hashfile", LL_ERROR);
			Server->Log("Starting image path repair...", LL_INFO);
			ServerUpdateStats::repairImages();
			Server->destroy(cc);
			return false;
		}
		std::string ts=server_identity+"INCR IMAGE letter="+pLetter+"&hashsize="+nconvert(hashfile->Size())+"&token="+server_token+chksum_str;
		size_t rc=tcpstack.Send(cc, ts);
		if(rc==0)
		{
			ServerLogger::Log(clientid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
			Server->destroy(cc);
			Server->destroy(hashfile);
			return false;
		}
		hashfile->Seek(0);
		char buffer[4096];
		for(size_t i=0,hsize=(size_t)hashfile->Size();i<hsize;i+=4096)
		{
			size_t tsend=(std::min)((size_t)4096, hsize-i);
			if(hashfile->Read(buffer, (_u32)tsend)!=tsend)
			{
				ServerLogger::Log(clientid, "Reading from hashfile failed", LL_ERROR);
				Server->destroy(cc);
				Server->destroy(hashfile);
				return false;
			}
			if(!cc->Write(buffer, tsend))
			{
				ServerLogger::Log(clientid, "Sending hashdata failed", LL_ERROR);
				Server->destroy(cc);
				Server->destroy(hashfile);
				return false;
			}
		}
		Server->destroy(hashfile);
	}

	std::wstring imagefn=constructImagePath(widen(sletter));
	
	int64 free_space=os_free_space(os_file_prefix(ExtractFilePath(imagefn)));
	if(free_space!=-1 && free_space<minfreespace_image)
	{
		ServerLogger::Log(clientid, "Not enough free space. Cleaning up.", LL_INFO);
		ServerCleanupThread cleanup;
		if(!cleanup.do_cleanup(minfreespace_image) )
		{
			ServerLogger::Log(clientid, "Could not free space for image. NOT ENOUGH FREE SPACE.", LL_ERROR);
			Server->destroy(cc);
			return false;
		}
	}

	{
		std::string mbrd=getMBR(widen(sletter));
		if(mbrd.empty())
		{
			if(pLetter!="SYSVOL")
			{
				ServerLogger::Log(clientid, "Error getting MBR data", LL_ERROR);
			}
		}
		else
		{
			IFile *mbr_file=Server->openFile(os_file_prefix(imagefn+L".mbr"), MODE_WRITE);
			if(mbr_file!=NULL)
			{
				_u32 w=mbr_file->Write(mbrd);
				if(w!=mbrd.size())
				{
					Server->Log("Error writing mbr data.", LL_ERROR);
					Server->destroy(mbr_file);
					Server->destroy(cc);
					return false;
				}
				Server->destroy(mbr_file);
			}
			else
			{
				Server->Log("Error creating file for writing MBR data.", LL_ERROR);
				Server->destroy(cc);
				return false;
			}
		}
	}

	if(pParentvhd.empty())
		backupid=createBackupImageSQL(0,0, clientid, imagefn, pLetter);
	else
		backupid=createBackupImageSQL(incremental, incremental_ref, clientid, imagefn, pLetter);

	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	bool first=true;
	char buffer[4096];
	unsigned int blocksize=0xFFFFFFFF;
	unsigned int blockleft=0;
	int64 currblock=-1;
	char *blockdata=NULL;
	int64 drivesize;
	ServerVHDWriter *vhdfile=NULL;
	THREADPOOL_TICKET vhdfile_ticket;
	IVHDFile *r_vhdfile=NULL;
	IFile *hashfile=NULL;
	IFile *parenthashfile=NULL;
	int64 blockcnt=0;
	int64 numblocks=0;
	int64 blocks=0;
	int64 totalblocks=0;
	int64 mbr_offset=0;
	_u32 off=0;
	std::string shadowdrive;
	int shadow_id=-1;
	bool persistent=false;
	unsigned char *zeroblockdata=NULL;
	int64 nextblock=0;
	int64 vhd_blocksize=(1024*1024)/2;
	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, true);
	Server->getThreadPool()->execute(running_updater);
	unsigned char verify_checksum[sha_size];

	bool has_parent=false;
	if(!pParentvhd.empty())
		has_parent=true;

	sha256_ctx shactx;
	sha256_init(&shactx);

	_i64 transferred_bytes=0;
	unsigned int image_backup_starttime=Server->getTimeMS();

	unsigned int curr_image_recv_timeout=image_recv_timeout;

	unsigned int stat_update_cnt=0;

	while(Server->getTimeMS()-starttime<=image_timeout)
	{
		if(ServerStatus::isBackupStopped(clientname))
		{
			ServerLogger::Log(clientid, L"Server admin stopped backup.", LL_WARNING);
			goto do_image_cleanup;
		}
		size_t r=cc->Read(&buffer[off], 4096-off, curr_image_recv_timeout);
		if(r!=0)
			r+=off;
		starttime=Server->getTimeMS();
		off=0;
		if(r==0 )
		{
			if(persistent && nextblock!=0)
			{
				int64 continue_block=nextblock;
				if(continue_block%vhd_blocksize!=0 )
				{
					continue_block=(continue_block/vhd_blocksize)*vhd_blocksize;
				}
				bool reconnected=false;
				while(Server->getTimeMS()-starttime<=image_timeout)
				{
					if(ServerStatus::isBackupStopped(clientname))
					{
						ServerLogger::Log(clientid, L"Server admin stopped backup. (2)", LL_WARNING);
						goto do_image_cleanup;
					}
					ServerStatus::setROnline(clientname, false);
					if(cc!=NULL)
					{
						transferred_bytes+=cc->getTransferedBytes();
						Server->destroy(cc);
					}
					Server->Log("Trying to reconnect in doImage", LL_DEBUG);
					cc=getClientCommandConnection(10000);
					if(cc==NULL)
					{
						std::string msg;
						std::vector<std::string> msgs;
						while(pipe->Read(&msg, 0)>0)
						{
							if(msg.find("address")==0)
							{
								IScopedLock lock(clientaddr_mutex);
								memcpy(&clientaddr, &msg[7], sizeof(sockaddr_in) );
							}
							else
							{
								msgs.push_back(msg);
							}
						}
						for(size_t i=0;i<msgs.size();++i)
							pipe->Write(msgs[i]);

						Server->wait(60000);
					}
					else
					{
						reconnected=true;
						ServerStatus::setROnline(clientname, true);
						Server->Log("Reconnected.", LL_DEBUG);
						break;
					}
				}

				if(!reconnected)
				{
					ServerLogger::Log(clientid, "Timeout while trying to reconnect", LL_ERROR);
					goto do_image_cleanup;
				}

				if(pParentvhd.empty())
				{
					tcpstack.Send(cc, server_identity+"FULL IMAGE letter="+pLetter+"&shadowdrive="+shadowdrive+"&start="+nconvert(continue_block)+"&shadowid="+nconvert(shadow_id));
				}
				else
				{
					std::string ts="INCR IMAGE letter=C:&shadowdrive="+shadowdrive+"&start="+nconvert(continue_block)+"&shadowid="+nconvert(shadow_id)+"&hashsize="+nconvert(parenthashfile->Size());
					size_t rc=tcpstack.Send(cc, server_identity+ts);
					if(rc==0)
					{
						ServerLogger::Log(clientid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
						goto do_image_cleanup;
					}
					parenthashfile->Seek(0);
					char buffer[4096];
					for(size_t i=0,hsize=(size_t)parenthashfile->Size();i<hsize;i+=4096)
					{
						size_t tsend=(std::min)((size_t)4096, hsize-i);
						if(parenthashfile->Read(buffer, (_u32)tsend)!=tsend)
						{
							ServerLogger::Log(clientid, "Reading from hashfile failed i="+nconvert(i), LL_ERROR);
							goto do_image_cleanup;
						}
						if(!cc->Write(buffer, tsend))
						{
							ServerLogger::Log(clientid, "Sending hashdata failed", LL_ERROR);
							goto do_image_cleanup;
						}
					}

				}
				off=0;
				starttime=Server->getTimeMS();

				blockleft=0;
				currblock=-1;
			}
			else
			{
				ServerLogger::Log(clientid, "Pipe to client unexpectedly closed has_error="+nconvert(cc->hasError()), LL_ERROR);
				goto do_image_cleanup;
			}
		}
		else
		{
			if(first)
			{
				first=false;
				size_t csize=sizeof(unsigned int);
				_u32 off_start=off;
				if(r>=csize)
				{
					memcpy(&blocksize, buffer, sizeof(unsigned int) );
					off+=sizeof(unsigned int);
					vhd_blocksize/=blocksize;
				}
				if(blocksize==0xFFFFFFFF)
				{
					off+=sizeof(unsigned int);
					if(r>sizeof(uint64))
					{
						std::string err;
						err.resize(r-sizeof(uint64) );
						memcpy(&err[0], &buffer[off], r-off);
						if(pLetter!="SYSVOL")
						{
							ServerLogger::Log(clientid, "Request of image backup failed. Reason: "+err, LL_ERROR);
						}
						else
						{
							ServerLogger::Log(clientid, "Request of SYSVOL failed. Reason: "+err, LL_INFO);
						}
					}
					else
					{
						ServerLogger::Log(clientid, "Error on client. No reason given.", LL_ERROR);
					}
					goto do_image_cleanup;
				}
				bool issmall=false;
				csize+=sizeof(int64);
				if(r>=csize)
				{
					memcpy(&drivesize, &buffer[off], sizeof(int64) );
					off+=sizeof(int64);

					blockcnt=drivesize/blocksize;
					blocks=blockcnt;
					totalblocks=blockcnt;

					if(drivesize%blocksize!=0)
						++totalblocks;

					zeroblockdata=new unsigned char[blocksize];
					memset(zeroblockdata, 0, blocksize);

					if(!has_parent)
						r_vhdfile=image_fak->createVHDFile(os_file_prefix(imagefn), false, drivesize+(int64)mbr_size, (unsigned int)vhd_blocksize*blocksize, true);
					else
						r_vhdfile=image_fak->createVHDFile(os_file_prefix(imagefn), pParentvhd, false, true);

					if(r_vhdfile==NULL)
					{
						ServerLogger::Log(clientid, L"Error opening VHD file \""+imagefn+L"\"", LL_ERROR);
						goto do_image_cleanup;
					}

					vhdfile=new ServerVHDWriter(r_vhdfile, blocksize, 5000, clientid);
					vhdfile_ticket=Server->getThreadPool()->execute(vhdfile);

					blockdata=vhdfile->getBuffer();

					hashfile=Server->openFile(os_file_prefix(imagefn+L".hash"), MODE_WRITE);
					if(hashfile==NULL)
					{
						ServerLogger::Log(clientid, L"Error opening Hashfile \""+imagefn+L".hash\"", LL_ERROR);
						goto do_image_cleanup;
					}

					if(has_parent)
					{
						parenthashfile=Server->openFile(os_file_prefix(pParentvhd+L".hash"), MODE_READ);
						if(parenthashfile==NULL)
						{
							ServerLogger::Log(clientid, L"Error opening Parenthashfile \""+pParentvhd+L".hash\"", LL_ERROR);
							goto do_image_cleanup;
						}
					}

					mbr_offset=writeMBR(vhdfile, drivesize);
				}
				else
				{
					issmall=true;
				}
				csize+=+sizeof(int64);
				if(r>=csize)
				{
					memcpy(&blockcnt, &buffer[off], sizeof(int64) );
					off+=sizeof(int64);
				}
				else
				{
					issmall=true;
				}
				csize+=1;
				if(r>=csize)
				{
					char c_persistent=buffer[off];
					if(c_persistent!=0)
						persistent=true;
					++off;
				}
				else
				{
					issmall=true;
				}

				unsigned int shadowdrive_size=0;
				csize+=sizeof(unsigned int);
				if(r>=csize)
				{
					memcpy(&shadowdrive_size, &buffer[off], sizeof(unsigned int));
					off+=sizeof(unsigned int);
					if(shadowdrive_size>0)
					{
						csize+=shadowdrive_size;
						if( r>=csize)
						{
							shadowdrive.resize(shadowdrive_size);
							memcpy(&shadowdrive[0],  &buffer[off], shadowdrive_size);
							off+=shadowdrive_size;
						}
						else
						{
							issmall=true;
						}
					}
				}
				else
				{
					issmall=true;
				}

				csize+=sizeof(int);
				if(r>=csize)
				{
					memcpy(&shadow_id, &buffer[off], sizeof(int));
					off+=sizeof(int);
				}
				else
				{
					issmall=true;
				}

				if(with_checksum)
				{
					csize+=sha_size;
					if(r>=csize)
					{
						sha256_init(&shactx);
						sha256_update(&shactx, (unsigned char*)&buffer[off_start], (unsigned int)csize-sha_size);
						unsigned char dig[sha_size];
						sha256_final(&shactx, dig);
						if(memcmp(dig, &buffer[off], sha_size)!=0)
						{
							ServerLogger::Log(clientid, "Checksum for first packet wrong. Stopping image backup.", LL_ERROR);
							goto do_image_cleanup;
						}
						off+=sha_size;
						sha256_init(&shactx);
					}
					else
					{
						issmall=true;
					}
				}

				if(issmall)
				{
					ServerLogger::Log(clientid, "First packet to small", LL_ERROR);
					goto do_image_cleanup;
				}

				curr_image_recv_timeout=image_recv_timeout_after_first;

				if(r==off)
				{
					off=0;
					continue;
				}
			}
			while(true)
			{
				if(blockleft==0)
				{
					if(currblock!=-1) // write current block
					{
						if(nextblock<=currblock)
						{
							++numblocks;
							++stat_update_cnt;
							if(stat_update_cnt%stat_update_skip==0)
							{
								stat_update_cnt=0;
								if(blockcnt!=0)
								{
									if(has_parent)
									{
										status.pcdone=(int)(((double)currblock/(double)totalblocks)*100.0+0.5);
									}
									else
									{
										status.pcdone=(int)(((double)numblocks/(double)blockcnt)*100.0+0.5);
									}
									ServerStatus::setServerStatus(status, true);
								}
							}

							nextblock=updateNextblock(nextblock, currblock, &shactx, zeroblockdata, has_parent, vhdfile, hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize);
							sha256_update(&shactx, (unsigned char *)blockdata, blocksize);

							vhdfile->writeBuffer(mbr_offset+currblock*blocksize, blockdata, blocksize);
							blockdata=vhdfile->getBuffer();

							if(nextblock%vhd_blocksize==0 && nextblock!=0)
							{
								Server->Log("Hash written "+nconvert(currblock), LL_DEBUG);
								sha256_final(&shactx, verify_checksum);
								hashfile->Write((char*)verify_checksum, sha_size);
								sha256_init(&shactx);
							}

							if(vhdfile->hasError())
							{
								ServerLogger::Log(clientid, "FATAL ERROR: Could not write to VHD-File", LL_ERROR);
								goto do_image_cleanup;
							}
						}

						currblock=-1;
					}
					bool accum=false;
					if(r-off>=sizeof(int64) )
					{
						memcpy(&currblock, &buffer[off], sizeof(int64) );
						if(currblock==-123)
						{
							if(!with_checksum)
							{
								nextblock=updateNextblock(nextblock, totalblocks, &shactx, zeroblockdata, has_parent, vhdfile, hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize);

								if(nextblock!=0)
								{
									//Server->Log("Hash written "+nconvert(nextblock), LL_INFO);
									unsigned char dig[sha_size];
									sha256_final(&shactx, dig);
									hashfile->Write((char*)dig, sha_size);
								}
							}

							transferred_bytes+=cc->getTransferedBytes();
							Server->destroy(cc);
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
							}
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);

							bool vhdfile_err=false;

							status.action_done=true;
							ServerStatus::setServerStatus(status);

							if(vhdfile!=NULL)
							{
								vhdfile->doExit();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								vhdfile_err=vhdfile->hasError();
								delete vhdfile;
								vhdfile=NULL;
							}

							IFile *t_file=Server->openFile(os_file_prefix(imagefn), MODE_READ);
							if(t_file!=NULL)
							{
								db->BeginTransaction();
								q_set_image_size->Bind(t_file->Size());
								q_set_image_size->Bind(backupid);
								q_set_image_size->Write();
								q_set_image_size->Reset();
								q_update_images_size->Bind(clientid);
								q_update_images_size->Bind(t_file->Size());
								q_update_images_size->Bind(clientid);
								q_update_images_size->Write();
								q_update_images_size->Reset();
								if(vhdfile_err==false)
								{
									setBackupImageComplete();
								}
								db->EndTransaction();
								Server->destroy(t_file);
							}

							running_updater->stop();
							updateRunning(true);

							unsigned int passed_time=Server->getTimeMS()-image_backup_starttime;
							if(passed_time==0) passed_time=1;

							ServerLogger::Log(clientid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );

							return !vhdfile_err;
						}
						else if(currblock==-124 ||
#ifndef _WIN32 
								currblock==0xFFFFFFFFFFFFFFFFLLU)
#else
								currblock==0xFFFFFFFFFFFFFFFF)
#endif
						{
							if(r-off>sizeof(int64))
							{
								std::string err;
								err.resize(r-off-sizeof(int64) );
								memcpy(&err[0], &buffer[off+sizeof(int64)], r-off-sizeof(int64));
								ServerLogger::Log(clientid, "Error on client occured: "+err, LL_ERROR);
							}
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								std::vector<THREADPOOL_TICKET> wf;wf.push_back(vhdfile_ticket);
								Server->getThreadPool()->waitFor(wf);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							delete []zeroblockdata;
							return false;
						}
						else if(currblock==-125) //ping
						{
							off+=sizeof(int64);
							currblock=-1;
						}
						else if(currblock==-126) //checksum
						{
							if(r-off>=2*sizeof(int64)+sha_size)
							{
								int64 hblock;
								unsigned char dig[sha_size];
								memcpy(&hblock, &buffer[off+sizeof(int64)], sizeof(int64));
								memcpy(&dig, &buffer[off+2*sizeof(int64)], sha_size);


								if( (nextblock<hblock || hblock==blocks) && hblock>0)
								{
									if(nextblock<hblock)
									{
										nextblock=updateNextblock(nextblock, hblock-1, &shactx, zeroblockdata, has_parent, vhdfile, hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize);
										sha256_update(&shactx, (unsigned char *)zeroblockdata, blocksize);						
									}
									if( (nextblock%vhd_blocksize==0 || hblock==blocks) && nextblock!=0)
									{
										sha256_final(&shactx, verify_checksum);
										hashfile->Write((char*)verify_checksum, sha_size);
										sha256_init(&shactx);
									}
								}

								if( memcmp(verify_checksum, dig, sha_size)!=0)
								{
									Server->Log("Client hash="+base64_encode(dig, sha_size)+" Server hash="+base64_encode(verify_checksum, sha_size), LL_DEBUG);
									ServerLogger::Log(clientid, "ERROR: Checksum for image block wrong. Stopping image backup.", LL_ERROR);
									goto do_image_cleanup;
								}

								off+=2*sizeof(int64)+sha_size;								
							}
							else
							{
								accum=true;
							}
							currblock=-1;
						}
						else
						{
							off+=sizeof(int64);
							blockleft=blocksize;
						}
					}
					else
					{
						accum=true;
					}

					if(accum)
					{
						if(r-off>0)
						{
							char buf2[4096];
							memcpy(buf2, &buffer[off], r-off);
							memcpy(buffer, buf2, r-off);
							off=(_u32)r-off;
							break;
						}
						else
						{
							off=0;
							break;
						}
					}
				}
				else
				{
					unsigned int available=(std::min)(blockleft, (unsigned int)r-off);
					memcpy(&blockdata[blocksize-blockleft], &buffer[off], available);
					blockleft-=available;
					off+=available;
					if( off>=r )
					{
						off=0;
						break;
					}
				}
			}
		}
	}
	ServerLogger::Log(clientid, "Timeout while transfering image data", LL_ERROR);
do_image_cleanup:
	transferred_bytes+=cc->getTransferedBytes();
	unsigned int passed_time=Server->getTimeMS()-image_backup_starttime;
	if(passed_time==0) passed_time=1;
	ServerLogger::Log(clientid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time) )), LL_INFO );
	Server->destroy(cc);
	if(vhdfile!=NULL)
	{
		if(blockdata!=NULL)
			vhdfile->freeBuffer(blockdata);

		vhdfile->doExitNow();
		Server->getThreadPool()->waitFor(vhdfile_ticket);
		delete vhdfile;
		vhdfile=NULL;
	}
	if(hashfile!=NULL) Server->destroy(hashfile);
	if(parenthashfile!=NULL) Server->destroy(parenthashfile);
	running_updater->stop();
	delete []zeroblockdata;
	return false;
}

unsigned int BackupServerGet::writeMBR(ServerVHDWriter *vhdfile, uint64 volsize)
{
	unsigned char *mbr=(unsigned char *)vhdfile->getBuffer();
	unsigned char *mptr=mbr;

	memcpy(mptr, mbr_code, 440);
	mptr+=440;
	int sig=rand();
	memcpy(mptr, &sig, sizeof(int));
	mptr+=sizeof(int);
	*mptr=0;
	++mptr;
	*mptr=0;
	++mptr;

	unsigned char partition[16];
	partition[0]=0x80;
	partition[1]=0xfe;
	partition[2]=0xff;
	partition[3]=0xff;
	partition[4]=0x07; //ntfs
	partition[5]=0xfe;
	partition[6]=0xff;
	partition[7]=0xff;
	partition[8]=0x00;
	partition[9]=0x04;
	partition[10]=0x00;
	partition[11]=0x00;

	unsigned int sectors=(unsigned int)(volsize/((uint64)sector_size));

	memcpy(&partition[12], &sectors, sizeof(unsigned int) );

	memcpy(mptr, partition, 16);
	mptr+=16;
	for(int i=0;i<3;++i)
	{
		memset(mptr, 0, 16);
		mptr+=16;
	}
	*mptr=0x55;
	++mptr;
	*mptr=0xaa;
	vhdfile->writeBuffer(0, (char*)mbr, 512);

	for(int i=0;i<1023;++i)
	{
		char *buf=vhdfile->getBuffer();
		memset(buf, 0, 512);
		vhdfile->writeBuffer((i+1)*512, buf, 512);
	}

	return 1024*512;
}

int64 BackupServerGet::updateNextblock(int64 nextblock, int64 currblock, sha256_ctx *shactx, unsigned char *zeroblockdata, bool parent_fn, ServerVHDWriter *parentfile, IFile *hashfile, IFile *parenthashfile, unsigned int blocksize, int64 mbr_offset, int64 vhd_blocksize)
{
	if(nextblock==currblock)
		return nextblock+1;
	else if(nextblock>currblock)
		return nextblock;

	if(currblock-nextblock>=vhd_blocksize)
	{
		if(nextblock%vhd_blocksize!=0)
		{
			while(true)
			{
				sha256_update(shactx, zeroblockdata, blocksize);
				++nextblock;

				if(nextblock%vhd_blocksize==0 && nextblock!=0)
				{
					unsigned char dig[sha_size];
					sha256_final(shactx, dig);
					hashfile->Write((char*)dig, sha_size);
					sha256_init(shactx);
					break;
				}
			}
		}

		while(currblock-nextblock>=vhd_blocksize)
		{
			if(!parent_fn)
			{
				hashfile->Write((char*)zero_hash, sha_size);
			}
			else
			{
				bool b=parenthashfile->Seek((nextblock/vhd_blocksize)*sha_size);
				if(!b)
				{
					Server->Log("Seeking in parenthashfile failed", LL_ERROR);
				}
				char dig[sha_size];
				_u32 rc=parenthashfile->Read(dig, sha_size);
				if(rc!=sha_size)
					Server->Log("Reading from parenthashfile failed", LL_ERROR);
				hashfile->Write(dig, sha_size);
			}
			nextblock+=vhd_blocksize;
		}
	}

	while(nextblock<currblock)
	{
		sha256_update(shactx, zeroblockdata, blocksize);
		++nextblock;
		if(nextblock%vhd_blocksize==0 && nextblock!=0)
		{
			unsigned char dig[sha_size];
			sha256_final(shactx, dig);
			hashfile->Write((char*)dig, sha_size);
			sha256_init(shactx);
		}
	}
	return nextblock+1;
}