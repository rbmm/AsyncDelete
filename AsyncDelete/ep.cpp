#include "stdafx.h"

#include "resource.h"
#include "task.h"
#include "msgbox.h"

void ShowTaskStatus(HWND hwnd, Task* task)
{
	WCHAR sz[0x80], tmp[32], tmp2[32];

	StrFormatByteSizeW(task->_M_sizes, tmp, _countof(tmp));
	StrFormatByteSizeW(task->_M_dwBytes, tmp2, _countof(tmp2));

	if (0 < swprintf_s(sz, _countof(sz), L"%u ms: %X[%ws]: %X(%X) Folders | %X(%X) Files (%ws)%ws", 
		GetTickCount() - task->_M_time, task->_M_MaxObjects, tmp2, 
		task->_M_Folders, task->_M_nDeletedFolders, task->_M_Files, task->_M_nDeletedFiles, tmp,
		task->_M_bCancelled ? L" cancelled" : L""))
	{
		SetDlgItemTextW(hwnd, -1, sz);
	}
}

void OnTaskEnd(HWND hwnd, Task* task)
{
	if (task->_M_nObjects)
	{
		__debugbreak();
	}

	ShowTaskStatus(hwnd, task);

	if (task->_M_status)
	{
		ShowErrorBox(hwnd, task->_M_status, task->_M_FailPath);

		if (task->_M_FailPath)
		{
			CustomMessageBox(hwnd, task->_M_FailPath, 0, MB_ICONHAND);
		}
	}
}

static NTSTATUS OnKdOk(HWND hwnd)
{
	SetDlgItemTextW(hwnd, -1, L"");

	NTSTATUS status = STATUS_OBJECT_PATH_INVALID;

	if (ULONG len = GetWindowTextLengthW(GetDlgItem(hwnd, IDC_EDIT1)))
	{
		status = STATUS_NO_MEMORY;

		if (PWSTR psz = new WCHAR[++len])
		{
			status = STATUS_UNSUCCESSFUL;
			UNICODE_STRING ObjectName;
			BOOL bDelete = FALSE;
			if (GetDlgItemTextW(hwnd, IDC_EDIT1, psz, len))
			{
				if (BST_CHECKED == SendDlgItemMessageW(hwnd, IDC_CHECK1, BM_GETCHECK, 0, 0))
				{
					if (IDYES != CustomMessageBox(hwnd, psz, L"Delete ? Are you shure ?", MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2))
					{
						status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
						goto __0;
					}
					bDelete = TRUE;
				}
				status = RtlDosPathNameToNtPathName_U_WithStatus(psz, &ObjectName, 0, 0);
			}
__0:
			delete [] psz;

			if (0 <= status)
			{
				status = STATUS_NO_MEMORY;
				if (Task* pTask = new Task(hwnd))
				{
					OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
					if (0 <= (status = Process(&oa, pTask, bDelete)))
					{
						SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)pTask);
						EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
						SetTimer(hwnd, 0, 1000, 0);
					}
					else
					{
						pTask->Release();
					}
				}

				RtlFreeUnicodeString(&ObjectName);
			}
		}
	}

	return status;
}

void SelectFolder(HWND hwnd)
{
	PWSTR pszFilePath = 0;
	IFileOpenDialog *pFileOpen;

	HRESULT hr = CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));

	if (0 <= hr)
	{
		if (0 <= (hr = pFileOpen->SetOptions(FOS_PICKFOLDERS|FOS_NOVALIDATE|FOS_NOTESTFILECREATE|FOS_DONTADDTORECENT|FOS_FORCESHOWHIDDEN)) &&
			0 <= (hr = pFileOpen->Show(hwnd)))
		{
			IShellItem *pItem;

			if (0 <= (hr = pFileOpen->GetResult(&pItem)))
			{
				if (0 <= (hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)))
				{
					SetDlgItemTextW(hwnd, IDC_EDIT1, pszFilePath);
					CoTaskMemFree(pszFilePath);
				}
				
				pItem->Release();
			}
		}

		pFileOpen->Release();
	}
}

static INT_PTR CALLBACK KdDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam)
{
	const static UINT _s_iid[] = { ICON_BIG, ICON_SMALL };
	switch (umsg)
	{
	case WM_INITDIALOG:
		SendDlgItemMessageW(hwnd, IDC_EDIT1, EM_SETCUEBANNER, TRUE, (LPARAM)L"Path to folder for delete");

		{
			umsg = _countof(_s_iid) - 1;
			INT cx[] = { GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CXSMICON) }, 
				cy[] = { GetSystemMetrics(SM_CYICON), GetSystemMetrics(SM_CYSMICON) };

			do 
			{
				//IDI_INFORMATION
				if (0 <= LoadIconWithScaleDown((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(1), cx[umsg], cy[umsg], (HICON*)&lParam))
				{
					SendMessageW(hwnd, WM_SETICON, _s_iid[umsg], lParam);
				}
			} while (umsg--);
		}

		break;

	case WM_NCDESTROY:
		if (Task* pTask = (Task*)GetWindowLongPtrW(hwnd, DWLP_USER))
		{
			pTask->Release();
		}
		umsg = _countof(_s_iid);
		do 
		{
			if (lParam = SendMessageW(hwnd, WM_GETICON, _s_iid[--umsg], 0))
			{
				DestroyIcon((HICON)lParam);
			}
		} while (umsg);

		break;

	case WM_APP:
		if (Task* pTask = (Task*)SetWindowLongPtrW(hwnd, DWLP_USER, 0))
		{
			OnTaskEnd(hwnd, pTask);
			pTask->Release();
			EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
		}
		KillTimer(hwnd, 0);
		break;

	case WM_TIMER:
		if (Task* pTask = (Task*)GetWindowLongPtrW(hwnd, DWLP_USER))
		{
			ShowTaskStatus(hwnd, pTask);
		}
		else
		{
			KillTimer(hwnd, 0);
		}
		break;

	case WM_COMMAND:
		switch (wParam)
		{
		case IDOK:
			if (!GetWindowLongPtrW(hwnd, DWLP_USER))
			{
				if (umsg = OnKdOk(hwnd))
				{
					ShowErrorBox(hwnd, umsg, 0);
				}
			}
			break;
		case IDCANCEL:
			if (Task* pTask = (Task*)GetWindowLongPtrW(hwnd, DWLP_USER))
			{
				pTask->_M_bCancelled = TRUE;
			}
			else
			{
				EndDialog(hwnd, ERROR_CANCELLED);
			}
			break;

		case IDC_BUTTON1:
			SelectFolder(hwnd);
			break;
		}
		break;
	}

	return 0;
}

#define echo(x) x
#define label(x) echo(x)##__LINE__

#define BEGIN_PRIVILEGES(name, n) static const union { TOKEN_PRIVILEGES name;\
struct { ULONG PrivilegeCount; LUID_AND_ATTRIBUTES Privileges[n];} label(_) = { n, {

#define LAA(se) {{se}, SE_PRIVILEGE_ENABLED }

#define END_PRIVILEGES }};};

BEGIN_PRIVILEGES(tp_br, 2)
LAA(SE_BACKUP_PRIVILEGE),
LAA(SE_RESTORE_PRIVILEGE),
END_PRIVILEGES

NTSTATUS ABR()
{
	NTSTATUS status;
	HANDLE hToken;

	if (0 <= (status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken)))
	{
		status = NtAdjustPrivilegesToken(hToken, FALSE, const_cast<PTOKEN_PRIVILEGES>(&tp_br), 0, 0, 0);
		NtClose(hToken);
	}

	return status;
}

void WINAPI ep(void*)
{
	NTSTATUS status = ABR();
	if (STATUS_SUCCESS == status)
	{
		if (0 <= (status = InitIo(0x1000)))
		{
			if (0 <= CoInitializeEx(0, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE))
			{
				DialogBoxParamW((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(IDD_DIALOG1), 0, KdDlgProc, 0);
				CoUninitialize();
			}
			CleanupIo();
		}
	}

	if (status)
	{
		ShowErrorBox(0, status, 0);
	}

	ExitProcess(0);
}
