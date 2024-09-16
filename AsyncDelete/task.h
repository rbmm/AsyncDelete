#pragma once

struct Task 
{
	LONG64 _M_dwBytes = 0;
	LONG64 _M_sizes = 0;

	HWND _M_hwnd;
	PWSTR _M_FailPath = 0;

	LONG _M_Files = 0;
	LONG _M_Folders = 1;
	LONG _M_nTaskCount = 1;
	LONG _M_nRefCount = 2;

	LONG _M_nObjects = 0;
	LONG _M_MaxObjects = 0;
	LONG _M_nDeletedFolders = 0;
	LONG _M_nDeletedFiles = 0;
	NTSTATUS _M_status = 0;

	ULONG _M_time = GetTickCount();
	BOOLEAN _M_bCancelled = FALSE;

	NTSTATUS SetStatus(NTSTATUS status)
	{
		return InterlockedCompareExchange(&_M_status, status, 0);
	}

	void SetFailPath(POBJECT_ATTRIBUTES poa);

	void SetError(NTSTATUS status, POBJECT_ATTRIBUTES poa)
	{
		if (!SetStatus(status))
		{
			SetFailPath(poa);
		}
	}

	void StartTask()
	{
		AddRef();
		InterlockedIncrement(&_M_nTaskCount);
	}

	void EndTask()
	{
		if (!InterlockedDecrement(&_M_nTaskCount))
		{
			PostMessageW(_M_hwnd, WM_APP, 0, 0);
		}
		Release();
	}

	void AddRef()
	{
		InterlockedIncrement(&_M_nRefCount);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_M_nRefCount))
		{
			delete this;
		}
	}

	Task(HWND hwnd) : _M_hwnd(hwnd)
	{
	}

private:

	~Task()
	{
		if (_M_FailPath)
		{
			LocalFree(_M_FailPath);
		}
	}
};

NTSTATUS InitIo(ULONG n);
void CleanupIo();
NTSTATUS Process(POBJECT_ATTRIBUTES poa, Task* task, BOOL bDelete);