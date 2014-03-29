#include "ServerCleanupDao.h"
#include "../../stringtools.h"

ServerCleanupDao::ServerCleanupDao(IDatabase *db)
	: db(db)
{
	createQueries();
}

ServerCleanupDao::~ServerCleanupDao(void)
{
	destroyQueries();
}

/**
* @-SQLGenAccess
* @func std::vector<SIncompleteImages> ServerCleanupDao::getIncompleteImages
* @return int id, string path
* @sql
*   SELECT id, path
*   FROM backup_images
*   WHERE 
*     complete=0 AND running<datetime('now','-300 seconds')
*/
std::vector<ServerCleanupDao::SIncompleteImages> ServerCleanupDao::getIncompleteImages(void)
{
	db_results res=q_getIncompleteImages->Read();
	std::vector<ServerCleanupDao::SIncompleteImages> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeImage
* @sql
*   DELETE FROM backup_images WHERE id=:id(int)
*/
void ServerCleanupDao::removeImage(int id)
{
	q_removeImage->Bind(id);
	q_removeImage->Write();
	q_removeImage->Reset();
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getClientsSortFilebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id AS id FROM clients c
*		INNER JOIN backups b ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDao::getClientsSortFilebackups(void)
{
	db_results res=q_getClientsSortFilebackups->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getClientsSortImagebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id AS id FROM clients c 
*		INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b
*				ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDao::getClientsSortImagebackups(void)
{
	db_results res=q_getClientsSortImagebackups->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getFullNumImages
* @return int id, string letter
* @sql
*   SELECT id, letter FROM backup_images 
*	WHERE clientid=:clientid(int) AND incremental=0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getFullNumImages(int clientid)
{
	q_getFullNumImages->Bind(clientid);
	db_results res=q_getFullNumImages->Read();
	q_getFullNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].letter=res[i][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageRef> ServerCleanupDao::getImageRefs
* @return int id, int complete
* @sql
*	SELECT id, complete FROM backup_images
*	WHERE incremental<>0 AND incremental_ref=:incremental_ref(int)
*/
std::vector<ServerCleanupDao::SImageRef> ServerCleanupDao::getImageRefs(int incremental_ref)
{
	q_getImageRefs->Bind(incremental_ref);
	db_results res=q_getImageRefs->Read();
	q_getImageRefs->Reset();
	std::vector<ServerCleanupDao::SImageRef> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].complete=watoi(res[i][L"complete"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getImagePath
* @return string path
* @sql
*	SELECT path FROM backup_images WHERE id=:id(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getImagePath(int id)
{
	q_getImagePath->Bind(id);
	db_results res=q_getImagePath->Read();
	q_getImagePath->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getIncrNumImages
* @return int id, string letter
* @sql
*	SELECT id,letter FROM backup_images
*	WHERE clientid=:clientid(int) AND incremental<>0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getIncrNumImages(int clientid)
{
	q_getIncrNumImages->Bind(clientid);
	db_results res=q_getIncrNumImages->Read();
	q_getIncrNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].letter=res[i][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getFullNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0
*   ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDao::getFullNumFiles(int clientid)
{
	q_getFullNumFiles->Bind(clientid);
	db_results res=q_getFullNumFiles->Read();
	q_getFullNumFiles->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getIncrNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0
*	ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDao::getIncrNumFiles(int clientid)
{
	q_getIncrNumFiles->Bind(clientid);
	db_results res=q_getIncrNumFiles->Read();
	q_getIncrNumFiles->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getClientName
* @return string name
* @sql
*	SELECT name FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getClientName(int clientid)
{
	q_getClientName->Bind(clientid);
	db_results res=q_getClientName->Read();
	q_getClientName->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getFileBackupPath
* @return string path
* @sql
*	SELECT path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getFileBackupPath(int backupid)
{
	q_getFileBackupPath->Bind(backupid);
	db_results res=q_getFileBackupPath->Read();
	q_getFileBackupPath->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::deleteFiles
* @sql
*	DELETE FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDao::deleteFiles(int backupid)
{
	q_deleteFiles->Bind(backupid);
	q_deleteFiles->Write();
	q_deleteFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeFileBackup
* @sql
*	DELETE FROM backups WHERE id=:backupid(int)
*/
void ServerCleanupDao::removeFileBackup(int backupid)
{
	q_removeFileBackup->Bind(backupid);
	q_removeFileBackup->Write();
	q_removeFileBackup->Reset();
}

/**
* @-SQLGenAccess
* @func SFileBackupInfo ServerCleanupDao::getFileBackupInfo
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDao::SFileBackupInfo ServerCleanupDao::getFileBackupInfo(int backupid)
{
	q_getFileBackupInfo->Bind(backupid);
	db_results res=q_getFileBackupInfo->Read();
	q_getFileBackupInfo->Reset();
	SFileBackupInfo ret = { false, 0, L"", L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackupInfo ServerCleanupDao::getImageBackupInfo
* @return int id, string backuptime, string path, string letter
* @sql
*	SELECT id, backuptime, path, letter FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::SImageBackupInfo ServerCleanupDao::getImageBackupInfo(int backupid)
{
	q_getImageBackupInfo->Bind(backupid);
	db_results res=q_getImageBackupInfo->Read();
	q_getImageBackupInfo->Reset();
	SImageBackupInfo ret = { false, 0, L"", L"", L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
		ret.letter=res[0][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::moveFiles
* @sql
*	INSERT INTO files_del
*		(backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del)
*	SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del
*		FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDao::moveFiles(int backupid)
{
	q_moveFiles->Bind(backupid);
	q_moveFiles->Write();
	q_moveFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeImageSize
* @sql
*	UPDATE clients
*	SET bytes_used_images=( (SELECT bytes_used_images
*						     FROM clients
*						     WHERE id=(
*									    SELECT clientid FROM backup_images
*									    WHERE id=:backupid(int)
*  									  )
*						    )
*						 -  (SELECT size_bytes
*						     FROM backup_images
*						     WHERE id=:backupid(int) )
*						  )
*	WHERE id=(SELECT clientid
*			  FROM backup_images
*			  WHERE id=:backupid(int))
*/
void ServerCleanupDao::removeImageSize(int backupid)
{
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	db_results res=q_removeImageSize->Read();
	q_removeImageSize->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::addToImageStats
* @sql
*	INSERT INTO del_stats (backupid, image, delsize, clientid, incremental)
*	SELECT id, 1 AS image, (size_bytes+:size_correction(int64)) AS delsize, clientid, incremental
*		FROM backup_images WHERE id=:backupid(int)
*/
void ServerCleanupDao::addToImageStats(int64 size_correction, int backupid)
{
	q_addToImageStats->Bind(size_correction);
	q_addToImageStats->Bind(backupid);
	q_addToImageStats->Write();
	q_addToImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::updateDelImageStats
* @sql
*	UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=:rowid(int64)
*/
void ServerCleanupDao::updateDelImageStats(int64 rowid)
{
	q_updateDelImageStats->Bind(rowid);
	q_updateDelImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getClientImages
* @return int id, string path
* @sql
*	SELECT id, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getClientImages(int clientid)
{
	q_getClientImages->Bind(clientid);
	db_results res=q_getClientImages->Read();
	q_getClientImages->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDao::getClientFileBackups
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int)
*/
std::vector<int> ServerCleanupDao::getClientFileBackups(int clientid)
{
	q_getClientFileBackups->Bind(clientid);
	db_results res=q_getClientFileBackups->Read();
	q_getClientFileBackups->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDao::getAssocImageBackups
* @return int assoc_id
* @sql
*	SELECT assoc_id FROM assoc_images WHERE img_id=:img_id(int)
*/
std::vector<int> ServerCleanupDao::getAssocImageBackups(int img_id)
{
	q_getAssocImageBackups->Bind(img_id);
	db_results res=q_getAssocImageBackups->Read();
	q_getAssocImageBackups->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"assoc_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerCleanupDao::getImageSize
* @return int64 size_bytes
* @sql
*	SELECT size_bytes FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::CondInt64 ServerCleanupDao::getImageSize(int backupid)
{
	q_getImageSize->Bind(backupid);
	db_results res=q_getImageSize->Read();
	q_getImageSize->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"size_bytes"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SClientInfo> ServerCleanupDao::getClients
* @return int id, string name
* @sql
*	SELECT id, name FROM clients
*/
std::vector<ServerCleanupDao::SClientInfo> ServerCleanupDao::getClients(void)
{
	db_results res=q_getClients->Read();
	std::vector<ServerCleanupDao::SClientInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].name=res[i][L"name"];
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func vector<SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient(int clientid)
{
	q_getFileBackupsOfClient->Bind(clientid);
	db_results res=q_getFileBackupsOfClient->Read();
	q_getFileBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SFileBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].backuptime=res[i][L"backuptime"];
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient
* @return int id, string backuptime, string letter, string path
* @sql
*	SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient(int clientid)
{
	q_getImageBackupsOfClient->Bind(clientid);
	db_results res=q_getImageBackupsOfClient->Read();
	q_getImageBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].backuptime=res[i][L"backuptime"];
		ret[i].letter=res[i][L"letter"];
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::findFileBackup
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int) AND path=:path(string)
*/
ServerCleanupDao::CondInt ServerCleanupDao::findFileBackup(int clientid, const std::wstring& path)
{
	q_findFileBackup->Bind(clientid);
	q_findFileBackup->Bind(path);
	db_results res=q_findFileBackup->Read();
	q_findFileBackup->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeDanglingFiles
* @sql
*	DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)
*/
void ServerCleanupDao::removeDanglingFiles(void)
{
	q_removeDanglingFiles->Write();
}

/**
* @-SQLGenAccess
* @func int64 ServerCleanupDao::getUsedStorage
* @return int64 used_storage
* @sql
*	SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDao::CondInt64 ServerCleanupDao::getUsedStorage(int clientid)
{
	q_getUsedStorage->Bind(clientid);
	db_results res=q_getUsedStorage->Read();
	q_getUsedStorage->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"used_storage"]);
	}
	return ret;
}



//@-SQLGenSetup
void ServerCleanupDao::createQueries(void)
{
	q_getIncompleteImages=db->Prepare("SELECT id, path FROM backup_images WHERE  complete=0 AND running<datetime('now','-300 seconds')", false);
	q_removeImage=db->Prepare("DELETE FROM backup_images WHERE id=?", false);
	q_getClientsSortFilebackups=db->Prepare("SELECT DISTINCT c.id AS id FROM clients c INNER JOIN backups b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_getClientsSortImagebackups=db->Prepare("SELECT DISTINCT c.id AS id FROM clients c  INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_getFullNumImages=db->Prepare("SELECT id, letter FROM backup_images  WHERE clientid=? AND incremental=0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_getImageRefs=db->Prepare("SELECT id, complete FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	q_getImagePath=db->Prepare("SELECT path FROM backup_images WHERE id=?", false);
	q_getIncrNumImages=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_getFullNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_getIncrNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_getClientName=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	q_getFileBackupPath=db->Prepare("SELECT path FROM backups WHERE id=?", false);
	q_deleteFiles=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	q_removeFileBackup=db->Prepare("DELETE FROM backups WHERE id=?", false);
	q_getFileBackupInfo=db->Prepare("SELECT id, backuptime, path FROM backups WHERE id=?", false);
	q_getImageBackupInfo=db->Prepare("SELECT id, backuptime, path, letter FROM backup_images WHERE id=?", false);
	q_moveFiles=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del FROM files WHERE backupid=?", false);
	q_removeImageSize=db->Prepare("UPDATE clients SET bytes_used_images=( (SELECT bytes_used_images FROM clients WHERE id=( SELECT clientid FROM backup_images WHERE id=? ) ) -  (SELECT size_bytes FROM backup_images WHERE id=? ) ) WHERE id=(SELECT clientid FROM backup_images WHERE id=?)", false);
	q_addToImageStats=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental) SELECT id, 1 AS image, (size_bytes+?) AS delsize, clientid, incremental FROM backup_images WHERE id=?", false);
	q_updateDelImageStats=db->Prepare("UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=?", false);
	q_getClientImages=db->Prepare("SELECT id, path FROM backup_images WHERE clientid=?", false);
	q_getClientFileBackups=db->Prepare("SELECT id FROM backups WHERE clientid=?", false);
	q_getAssocImageBackups=db->Prepare("SELECT assoc_id FROM assoc_images WHERE img_id=?", false);
	q_getImageSize=db->Prepare("SELECT size_bytes FROM backup_images WHERE id=?", false);
	q_getClients=db->Prepare("SELECT id, name FROM clients", false);
	q_getFileBackupsOfClient=db->Prepare("SELECT id, backuptime, path FROM backups WHERE clientid=?", false);
	q_getImageBackupsOfClient=db->Prepare("SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=?", false);
	q_findFileBackup=db->Prepare("SELECT id FROM backups WHERE clientid=? AND path=?", false);
	q_removeDanglingFiles=db->Prepare("DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)", false);
	q_getUsedStorage=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=?", false);
}

//@-SQLGenDestruction
void ServerCleanupDao::destroyQueries(void)
{
	db->destroyQuery(q_getIncompleteImages);
	db->destroyQuery(q_removeImage);
	db->destroyQuery(q_getClientsSortFilebackups);
	db->destroyQuery(q_getClientsSortImagebackups);
	db->destroyQuery(q_getFullNumImages);
	db->destroyQuery(q_getImageRefs);
	db->destroyQuery(q_getImagePath);
	db->destroyQuery(q_getIncrNumImages);
	db->destroyQuery(q_getFullNumFiles);
	db->destroyQuery(q_getIncrNumFiles);
	db->destroyQuery(q_getClientName);
	db->destroyQuery(q_getFileBackupPath);
	db->destroyQuery(q_deleteFiles);
	db->destroyQuery(q_removeFileBackup);
	db->destroyQuery(q_getFileBackupInfo);
	db->destroyQuery(q_getImageBackupInfo);
	db->destroyQuery(q_moveFiles);
	db->destroyQuery(q_removeImageSize);
	db->destroyQuery(q_addToImageStats);
	db->destroyQuery(q_updateDelImageStats);
	db->destroyQuery(q_getClientImages);
	db->destroyQuery(q_getClientFileBackups);
	db->destroyQuery(q_getAssocImageBackups);
	db->destroyQuery(q_getImageSize);
	db->destroyQuery(q_getClients);
	db->destroyQuery(q_getFileBackupsOfClient);
	db->destroyQuery(q_getImageBackupsOfClient);
	db->destroyQuery(q_findFileBackup);
	db->destroyQuery(q_removeDanglingFiles);
	db->destroyQuery(q_getUsedStorage);
}