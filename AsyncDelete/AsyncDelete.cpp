#include "stdafx.h"

#include "io.h"
#include "task.h"

#define FILE_SHARE_VALID_FLAGS (FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE)

void Task::SetFailPath(POBJECT_ATTRIBUTES poa)
{
	ULONG cb = 0x10000;

	if (PVOID buf = LocalAlloc(LMEM_FIXED, cb))
	{
		union {
			PVOID pv;
			PWSTR psz;
			PFILE_NAME_INFORMATION pfni;
			PBYTE pb;
		};

		pv = buf;

		if (poa->RootDirectory)
		{
			IO_STATUS_BLOCK iosb;
			if (0 <= NtQueryInformationFile(poa->RootDirectory, &iosb, pfni, MINSHORT*sizeof(WCHAR), FileNameInformation))
			{
				ULONG FileNameLength = pfni->FileNameLength;
				memcpy(psz, pfni->FileName, FileNameLength);

				pb += FileNameLength;
				if (sizeof(WCHAR) < (cb -= FileNameLength))
				{
					*psz++ = '\\';
					cb -= sizeof(WCHAR);
				}
			}
		}

		if (cb >>= 1)
		{
			if (poa->ObjectName)
			{
				swprintf_s(psz, cb, L"%wZ", poa->ObjectName);
			}
		}

		if (pv = InterlockedExchangePointer((void**)&_M_FailPath, buf))
		{
			delete [] psz;
		}
	}
}

class __declspec(align(__alignof(SLIST_ENTRY))) CFolder : public IO_OBJECT
{
	enum { er = 'LLLL' };

	inline static ULONG _S_n;
	inline static SLIST_HEADER _S_Head;
	inline static PVOID _S_pFolders = 0;

	Task* _M_pTask;
	CFolder* _M_parent;
	LONG _M_Files = 1;
	ULONG _M_nLevel;
	ULONG _M_hash;
	BOOL _M_bDelete;
	union {
		FILE_DIRECTORY_INFORMATION _M_fdi[];
		UCHAR _M_buf[0x1000];
	};

	void DecFiles()
	{
		if (!InterlockedDecrement(&_M_Files))
		{
			InterlockedIncrementNoFence(&_M_pTask->_M_nDeletedFolders);
			Close();
			if (_M_parent)
			{
				_M_parent->DecFiles();
			}
		}
	}

	void IncFiles()
	{
		InterlockedIncrementNoFence(&_M_Files);
	}

	void Process(PFILE_DIRECTORY_INFORMATION pfdi)
	{
		Task* pTask = _M_pTask;
		BOOL bDelete = _M_bDelete;

		ULONG NextEntryOffset = 0;

		UNICODE_STRING ObjectName;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), getHandle(), &ObjectName, OBJ_CASE_INSENSITIVE };
		
		do 
		{
			if (pTask->_M_bCancelled)
			{
				return;
			}

			(ULONG_PTR&)pfdi += NextEntryOffset;

			switch (pfdi->FileNameLength)
			{
			case 2*sizeof(WCHAR):
				if ('.' != pfdi->FileName[1]) break;
			case sizeof(WCHAR):
				if ('.' == pfdi->FileName[0]) continue;
			}

			ObjectName.MaximumLength = ObjectName.Length = (USHORT)pfdi->FileNameLength;
			ObjectName.Buffer = pfdi->FileName;

			HANDLE hFile;
			IO_STATUS_BLOCK iosb;

			if (bDelete)
			{
				if (FILE_ATTRIBUTE_READONLY & pfdi->FileAttributes)
				{
					if (0 <= NtOpenFile(&hFile, FILE_WRITE_ATTRIBUTES, &oa, &iosb, FILE_SHARE_VALID_FLAGS, 
						FILE_OPEN_FOR_BACKUP_INTENT|FILE_OPEN_REPARSE_POINT))
					{
						static FILE_BASIC_INFORMATION fbi = { {}, {}, {}, {}, FILE_ATTRIBUTE_NORMAL };
						NtSetInformationFile(hFile, &iosb, &fbi, sizeof(fbi), FileBasicInformation);
						NtClose(hFile);
					}
				}
			}

			if (FILE_ATTRIBUTE_DIRECTORY & pfdi->FileAttributes)
			{
				InterlockedIncrementNoFence(&pTask->_M_Folders);

				if (CFolder* p = new CFolder(pTask, this, bDelete, _M_nLevel + 1))
				{
					p->Start(&oa);
					p->Release();
				}
			}
			else
			{
				InterlockedIncrementNoFence(&pTask->_M_Files);
				InterlockedExchangeAddNoFence64(&pTask->_M_sizes, pfdi->EndOfFile.QuadPart);

				if (bDelete)
				{
					NTSTATUS status;

					if (0 <= (status = NtOpenFile(&hFile, DELETE, &oa, &iosb, FILE_SHARE_VALID_FLAGS, 
						FILE_OPEN_FOR_BACKUP_INTENT|FILE_OPEN_REPARSE_POINT|FILE_DELETE_ON_CLOSE)))
					{
						NtClose(hFile);
					}

					if (0 > status)
					{
						if (0 <= (status = NtOpenFile(&hFile, DELETE, &oa, &iosb, FILE_SHARE_VALID_FLAGS, 
							FILE_OPEN_FOR_BACKUP_INTENT|FILE_OPEN_REPARSE_POINT)))
						{
							static const FILE_DISPOSITION_INFO_EX fdi = {
								FILE_DISPOSITION_FLAG_DELETE|FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
							};
							status = NtSetInformationFile(hFile, &iosb, (void*)&fdi, sizeof(fdi), FileDispositionInformationEx);
							NtClose(hFile);
						}
					}

					if (0 > status)
					{
						pTask->SetError(status, &oa);
					}
					else
					{
						InterlockedIncrementNoFence(&pTask->_M_nDeletedFiles);
					}
				}
			}

		} while (NextEntryOffset = pfdi->NextEntryOffset);
	}

	void Query()
	{
		if (NT_IRP* irp = new NT_IRP(this, er))
		{
			NTSTATUS status;

			status = NtQueryDirectoryFile(getHandle(), 0, 0, irp, irp, 
				_M_buf, sizeof(_M_buf), FileDirectoryInformation, FALSE, 0, FALSE);

			irp->CheckNtStatus(this, status);
		}
	}

	virtual void IOCompletionRoutine(
		DWORD Code, 
		NTSTATUS status, 
		ULONG_PTR dwNumberOfBytesTransfered, 
		PVOID /*Pointer*/)
	{
		if (er != Code)
		{
			__debugbreak();
		}

		if (0 > status)
		{
			if (STATUS_NO_MORE_FILES == status)
			{
				DecFiles();
				return;
			}

			return;
		}

		if (dwNumberOfBytesTransfered)
		{
			InterlockedExchangeAddNoFence64(&_M_pTask->_M_dwBytes, dwNumberOfBytesTransfered);
			Process(_M_fdi);
		}

		if (!_M_pTask->_M_bCancelled)
		{
			Query();
		}
	}

	NTSTATUS Open(POBJECT_ATTRIBUTES poa)
	{
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;
		ACCESS_MASK DesiredAccess = FILE_LIST_DIRECTORY;
		ULONG OpenOptions = FILE_DIRECTORY_FILE|FILE_OPEN_FOR_BACKUP_INTENT|FILE_OPEN_REPARSE_POINT;

		if (_M_bDelete)
		{
			DesiredAccess |= DELETE;
			OpenOptions |= FILE_DELETE_ON_CLOSE;
		}

		NTSTATUS status = NtOpenFile(&hFile, DesiredAccess, poa, &iosb, FILE_SHARE_VALID_FLAGS, OpenOptions);

		if (0 <= status)
		{
			if (0 <= (status = NT_IRP::BindIoCompletion(this, hFile)))
			{
				Assign(hFile);
				return STATUS_SUCCESS;
			}

			NtClose(hFile);
		}

		_M_pTask->SetError(status, poa);

		return status;
	}

	~CFolder()
	{
		//DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
		if (_M_parent)
		{
			_M_parent->Release();
			_M_parent = 0;
		}

		InterlockedDecrementNoFence(&_M_pTask->_M_nObjects);
		_M_pTask->EndTask();
		_M_pTask = 0;
	}

public:

	CFolder(Task* pTask, CFolder* parent, BOOL bDelete, ULONG nLevel) 
		: _M_pTask(pTask), _M_nLevel(nLevel), _M_parent(parent), _M_bDelete(bDelete)
	{
		//DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
		pTask->StartTask();
		if (parent) 
		{
			parent->AddRef();
			parent->IncFiles();
		}

		LONG nObjects = InterlockedIncrementNoFence(&pTask->_M_nObjects);

		LONG MaxObjects = 0, NewMaxObjects;
		do
		{
			NewMaxObjects = InterlockedCompareExchange(&pTask->_M_MaxObjects, nObjects, MaxObjects);
			if (MaxObjects == NewMaxObjects)
			{
				return ;
			}
		} while ((MaxObjects = NewMaxObjects) < nObjects);
	}

	void* operator new(size_t s)
	{
		if (sizeof(CFolder) < s)
		{
			__debugbreak();
		}

		if (PVOID pv = RtlInterlockedPopEntrySList(&_S_Head))
		{
			return pv;
		}

		return LocalAlloc(LMEM_FIXED, s);
	}

	void operator delete(PVOID pv)
	{
		if ((ULONG_PTR)pv - (ULONG_PTR)_S_pFolders < _S_n * sizeof(CFolder))
		{
			RtlInterlockedPushEntrySList(&_S_Head, (PSLIST_ENTRY)pv);

			return;
		}

		LocalFree(pv);
	}

	static void DestroyPool()
	{
		if (_S_pFolders)
		{
			if (RtlQueryDepthSList(&_S_Head) != _S_n)
			{
				__debugbreak();
			}
			delete [] _S_pFolders;
		}
	}

	NTSTATUS Start(POBJECT_ATTRIBUTES poa)
	{
		NTSTATUS status = Open(poa);
		if (0 <= status)
		{
			Query();
		}
		return status;
	}

	static NTSTATUS InitPool(ULONG n)
	{
		_S_n = n;
		RtlInitializeSListHead(&_S_Head);

		union {
			PUCHAR pb;
			CFolder* pFolder;
		};

		if (pb = new UCHAR[n * sizeof(CFolder)])
		{
			_S_pFolders = pb;
			do 
			{
				RtlInterlockedPushEntrySList(&_S_Head, (PSLIST_ENTRY)pFolder++);
			} while (--n);

			return S_OK;
		}

		return STATUS_NO_MEMORY;
	}
};

NTSTATUS InitIo(ULONG n)
{
	NTSTATUS status = TpAllocPool(&IO_OBJECT::_G_Pool, 0);
	if (0 <= status)
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		TpSetPoolMaxThreads(IO_OBJECT::_G_Pool, si.dwNumberOfProcessors * 2);

		return CFolder::InitPool(n);
	}

	return status;
}

void CleanupIo()
{
	CFolder::DestroyPool();
	if (IO_OBJECT::_G_Pool)
	{
		TpReleasePool(IO_OBJECT::_G_Pool);
	}
}

NTSTATUS Process(POBJECT_ATTRIBUTES poa, Task* task, BOOL bDelete)
{
	NTSTATUS status = STATUS_NO_MEMORY;

	if (CFolder* p = new CFolder(task, 0, bDelete, 0))
	{
		status = p->Start(poa);
		p->Release();
	}

	task->EndTask();

	return status;
}