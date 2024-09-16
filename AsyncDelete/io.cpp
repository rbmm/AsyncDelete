#include "stdafx.h"
#include "io.h"

void IO_OBJECT::StartIo()
{
	AddRef();
	if (m_Io) TpStartAsyncIoOperation(m_Io);
}

IO_OBJECT::~IO_OBJECT()
{
	if (m_Io)
	{
		TpReleaseIoCompletion(m_Io);
	}
	CloseObjectHandle(m_hFile);
}

void IO_OBJECT::CloseObjectHandle(HANDLE hFile)
{
	if (hFile) NtClose(hFile);
}

void IO_OBJECT::Close()
{
	if (HANDLE hFile = InterlockedExchangePointer(&m_hFile, 0))
	{
		CloseObjectHandle(hFile); 
	}
}

NTSTATUS IO_OBJECT::BindIoCompletionCB(HANDLE hFile, PTP_IO_CALLBACK Callback)
{
	TP_CALLBACK_ENVIRON CallbackEnviron, *pCallbackEnviron = 0;
	if (_G_Pool)
	{
		TpInitializeCallbackEnviron(&CallbackEnviron);
		TpSetCallbackThreadpool(&CallbackEnviron, _G_Pool);
		pCallbackEnviron = &CallbackEnviron;
	}
	return TpAllocIoCompletion(&m_Io, hFile, Callback, this, pCallbackEnviron);
}

void NT_IRP::CheckNtStatus(IO_OBJECT* pObj, NTSTATUS status, BOOL bSkippedOnSynchronous)
{
	if (status == STATUS_PENDING)
	{
		return ;
	}

	if (NT_ERROR(status) || bSkippedOnSynchronous)
	{
		TpCancelAsyncIoOperation(pObj->m_Io);
		Status = status;
		OnIoComplete(pObj, this);
	}
}

NT_IRP::NT_IRP(IO_OBJECT* pObj, DWORD Code, PVOID Ptr)
{
	Status = STATUS_PENDING;
	Information = 0;
	Pointer = Ptr;
	m_Code = Code;
	pObj->StartIo();
}

VOID NT_IRP::OnIoComplete(_Inout_opt_ PVOID Context, _In_ PIO_STATUS_BLOCK IoSB)
{
	reinterpret_cast<IO_OBJECT*>(Context)->IOCompletionRoutine(m_Code, IoSB->Status, IoSB->Information, Pointer);
	reinterpret_cast<IO_OBJECT*>(Context)->Release();
}

VOID NTAPI NT_IRP::S_OnIoComplete(
								  _Inout_ PTP_CALLBACK_INSTANCE /*Instance*/,
								  _Inout_opt_ PVOID Context,
								  _In_ PVOID ApcContext,
								  _In_ PIO_STATUS_BLOCK IoSB,
								  _In_ PTP_IO /*Io*/
								  )
{
	reinterpret_cast<NT_IRP*>(ApcContext)->OnIoComplete(Context, IoSB);
}

VOID NTAPI NT_IRP::ApcRoutine (
							   PVOID ApcContext,
							   PIO_STATUS_BLOCK IoSB,
							   ULONG /*Reserved*/
							   )
{
	static_cast<NT_IRP*>(IoSB)->OnIoComplete(ApcContext, IoSB);
}