#pragma once

class __declspec(novtable) IO_OBJECT 
{
	friend class NT_IRP;

	NTSTATUS BindIoCompletionCB(HANDLE hFile, PTP_IO_CALLBACK Callback);	

	virtual void IOCompletionRoutine(DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer) = 0;

	HANDLE				m_hFile = 0;
	PTP_IO				m_Io = 0;
	LONG				m_nRef = 1;

	void StartIo();

protected:

	IO_OBJECT() = default;

	virtual ~IO_OBJECT();

	virtual void CloseObjectHandle(HANDLE hFile);

public:

	static inline PTP_POOL _G_Pool = 0;

	HANDLE getHandle() { return m_hFile; }

	void AddRef()
	{
		InterlockedIncrementNoFence(&m_nRef);
	}

	void Release()
	{
		if (!InterlockedDecrement(&m_nRef)) delete this;
	}

	void Assign(HANDLE hFile)
	{
		m_hFile = hFile;
	}

	void Close();
};

class NT_IRP : public IO_STATUS_BLOCK 
{
	PVOID Pointer;
	DWORD m_Code;

	VOID OnIoComplete(PVOID Context, PIO_STATUS_BLOCK IoSB);

public:

	static NTSTATUS BindIoCompletion( IO_OBJECT* pObj, HANDLE hFile)
	{
		return pObj->BindIoCompletionCB(hFile, S_OnIoComplete);
	}

	static VOID NTAPI ApcRoutine (
		PVOID ApcContext,
		PIO_STATUS_BLOCK IoSB,
		ULONG /*Reserved*/
		);

	static VOID NTAPI S_OnIoComplete(
		_Inout_ PTP_CALLBACK_INSTANCE Instance,
		_Inout_opt_ PVOID Context,
		_In_ PVOID ApcContext,
		_In_ PIO_STATUS_BLOCK IoSB,
		_In_ PTP_IO Io
		);

	NT_IRP(IO_OBJECT* pObj, DWORD Code, PVOID Ptr = 0);

	void CheckNtStatus(IO_OBJECT* pObj, NTSTATUS status, BOOL bSkippedOnSynchronous = FALSE);

};
