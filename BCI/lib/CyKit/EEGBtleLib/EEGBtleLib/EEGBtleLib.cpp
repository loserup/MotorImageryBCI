/*

	Author:  CaptainSmiley
	References:  
		https://social.msdn.microsoft.com/Forums/vstudio/en-US/bad452cb-4fc2-4a86-9b60-070b43577cc9/is-there-a-simple-example-desktop-programming-c-for-bluetooth-low-energy-devices?forum=wdk
		and probably other sites too
	Notes:
		this version should allow easy integration into CyKit
*/

#include "stdafx.h"
#include "EEGBtleLib.h"

#include <iostream>
#include <stdio.h>
#include <string>
#include <windows.h>
#include <strsafe.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <rpc.h>
#include <Objbase.h>
#include <bthdef.h>
#include <bluetoothleapis.h>

#pragma comment(lib, "SetupAPI")
#pragma comment(lib, "BluetoothApis.lib")
#pragma comment(lib, "Ole32.lib")


//  this is a hack to be able to read the btle data easily;
typedef struct _MY_BTH_LE_GATT_CHARACTERISTIC_VALUE {
	ULONG DataSize;
	UCHAR Data[20];
} MY_BTH_LE_GATT_CHARACTERISTIC_VALUE, *PMYBTH_LE_GATT_CHARACTERISTIC_VALUE;


void *callback_fp;
void *error_fp;
bool func_cb = false;

std::wstring friendlyName;


void PrintError(LPTSTR lpszFunction)
{
	// Retrieve the system error message for the last-error code

	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	// Display the error message and exit the process

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		lpszFunction, dw, lpMsgBuf);
//	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}


/*
	FUNCTION:	set_callback_func
	PURPOSE:	this function is used to set the callback function pointer for the python module.
*/
void set_callback_func(void *fp)
{
	callback_fp = fp;
	func_cb = true;

//	((void(*)())callback_fp)();
}


void set_error_func(void *fp)
{
	error_fp = fp;
}


void PythonPrintError(PCHAR Err)
{
	((void(*)(PCHAR))error_fp)(Err);
}


const wchar_t* get_bluetooth_id(void)
{
	return friendlyName.c_str();
}


/*
	FUNCTION:	ProcessEvent
	PURPOSE:	This function holds the callback used by BluetoothGATTRegisterEvent.  It is called
				whenever the characteristic value changes or is updated
*/
void CALLBACK ProcessEvent(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter, PVOID Context)
{
	PBLUETOOTH_GATT_VALUE_CHANGED_EVENT ValueChangedEventParameters = (PBLUETOOTH_GATT_VALUE_CHANGED_EVENT)EventOutParameter;

	if (ValueChangedEventParameters->CharacteristicValue->DataSize != 0)
	{
		if (func_cb)
		{
			//  ****  NEED TO FIX THIS SO WE ARE PASSING POINTERS PROPERLY and not using the struct hack
			PMYBTH_LE_GATT_CHARACTERISTIC_VALUE pmv = (PMYBTH_LE_GATT_CHARACTERISTIC_VALUE)ValueChangedEventParameters->CharacteristicValue;
			((void(*)(MY_BTH_LE_GATT_CHARACTERISTIC_VALUE))callback_fp)(*pmv);
		}
	}
}


void QueryRegistryEntry(HKEY hKey)
{
	DWORD cValues, i, retCode;

	TCHAR  achValue[16383];
	DWORD cchValue = 16383;

	PBYTE fName = new BYTE[1];

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		NULL,                // buffer for class name 
		NULL,           // size of class string 
		NULL,                    // reserved 
		NULL,               // number of subkeys 
		NULL,            // longest subkey size 
		NULL,            // longest class string 
		&cValues,                // number of values for this key 
		NULL,            // longest value name 
		NULL,         // longest value data 
		NULL,   // security descriptor 
		NULL);       // last write time 

	// Enumerate the key values. 
	if (cValues)
	{
		for (i = 0, retCode = ERROR_SUCCESS; i<cValues; i++)
		{
			cchValue = 16383;			//  max unicode character limit
			achValue[0] = '\0';
			retCode = RegEnumValue(hKey, i,
				achValue,
				&cchValue,
				NULL,
				NULL,
				NULL,
				NULL);

			if (retCode == ERROR_SUCCESS)
			{
//				_tprintf(TEXT("(%d) %s\n"), i + 1, achValue);

				DWORD dataLen = 0;
				DWORD dataType = 0;

				DWORD err = RegQueryValueEx(hKey,
					TEXT("FriendlyName"),
					NULL,
					NULL,
					(PBYTE)fName,
					&dataLen);

				if (err == ERROR_MORE_DATA)
				{
					dataLen += 2;
					fName = new BYTE[dataLen];
					RtlZeroMemory(fName, dataLen);

					err = RegQueryValueEx(hKey,
						TEXT("FriendlyName"),
						NULL,
						NULL,
						(PBYTE)fName,
						&dataLen);
				}

				if (err == ERROR_SUCCESS)
				{
					friendlyName = (wchar_t *)fName;
					break;
				}
			}
		}
	}

	delete[] fName;
}


DWORD GetBluetoothDeviceID(HDEVINFO hDI, SP_DEVINFO_DATA dd)
{
	HKEY hKey = SetupDiOpenDevRegKey(hDI,
		&dd,
		DICS_FLAG_GLOBAL,
		0,
		DIREG_DEV,
		KEY_READ);

	if (hKey == INVALID_HANDLE_VALUE)
	{
		PythonPrintError((PCHAR)"SetupDiOpenDevRegKey Error");
		return (DWORD)-1;
	}

	QueryRegistryEntry(hKey);

	if (hKey != INVALID_HANDLE_VALUE)
	{
		DWORD dataLen = 0;
		DWORD dataType = 0;
		PBYTE uniqueName = new BYTE[1];

		DWORD err = RegQueryValueEx(hKey,
			TEXT("Bluetooth_UniqueID"),
			NULL,
			&dataType,
			(PBYTE)uniqueName,
			&dataLen);

		if (err == ERROR_MORE_DATA)
		{
			uniqueName = new BYTE[dataLen + 2];
			RtlZeroMemory(uniqueName, dataLen + 2);
		}

		err = RegQueryValueEx(hKey,
			TEXT("Bluetooth_UniqueID"),
			NULL,
			&dataType,
			(PBYTE)uniqueName,
			&dataLen);

		if (err == ERROR_MORE_DATA)
		{
			PythonPrintError((PCHAR)"GetBluetoothDeviceID: dala length error!");
			delete[] uniqueName;
			return (DWORD)-1;
		}

		std::wstring ws;
		wchar_t *pwc = NULL;
		wchar_t *next_token = NULL;
		pwc = wcstok_s((wchar_t *)uniqueName, L"_", &next_token);
		while (pwc != NULL)
		{
			ws = pwc;
			pwc = wcstok_s(NULL, L"_", &next_token);
		}

//		std::wcout << TEXT("Registry Value: ") << ws << std::endl;

		WCHAR name[255] = { 0 };

		wcscat_s(name, _countof(name), L"SYSTEM\\ControlSet001\\Enum\\BTHLE\\Dev_");
		wcscat_s(name, _countof(name), ws.c_str());

//		std::wcout << name << L"\n" << std::endl;

		delete[] uniqueName;

		LONG ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, name, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hKey);
		if (ret != ERROR_SUCCESS)
		{
			PythonPrintError((PCHAR)"RegOpenKeyEx error!");
			delete[] name;
			return (DWORD)-1;
		}
		else
		{
			PWCHAR tmpName;
			DWORD index = 0;
			DWORD dwSize = 0, dwIdx = 0;

			RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, &dwSize, NULL, NULL, NULL, NULL, NULL, NULL);

			dwSize += 2;
			tmpName = new WCHAR[dwSize];
			RtlZeroMemory(tmpName, dwSize);

			while (ERROR_SUCCESS == RegEnumKeyEx(hKey, index, tmpName, &dwSize, NULL, NULL, NULL, NULL))
			{
//				std::wcout << "Val: " << tmpName << std::endl;

				wcscat_s(name, _countof(name), L"\\");
				wcscat_s(name, _countof(name), tmpName);
//				std::wcout << name << std::endl;

				HKEY tmpKey;
				RegOpenKey(HKEY_LOCAL_MACHINE, name, &tmpKey);
				QueryRegistryEntry(tmpKey);
				RegCloseKey(tmpKey);

				RtlZeroMemory(tmpName, dwSize);

				++index;
			}

			delete[] tmpName;
		}

		RegCloseKey(hKey);
	}
	else
	{
		PythonPrintError((PCHAR)"GetBluetoothDeviceID: handle error!");
		return (DWORD)-1;
	}

	return ERROR_SUCCESS;
}



/*
	FUNCTION:	run_data_collection
	PURPOSE:	connect to the bluetooth device, find the characteristics and descriptors, and
				start the data collection
*/
void run_data_collection(HANDLE hLEDevice, const WCHAR **uuids)
{
	USHORT serviceBufferCount;
	USHORT numServices;
	USHORT charBufferSize;
	PBTH_LE_GATT_CHARACTERISTIC pCharBuffer = NULL;
	PBTH_LE_GATT_SERVICE pServiceBuffer;
	OLECHAR *pStr;
	HRESULT resSvc, resCh, resRE, resGD, resGDV, resSDV, resSCV;
	USHORT numChars;
	BOOL found = FALSE;

	if((resSvc = BluetoothGATTGetServices(hLEDevice,
										0,
										NULL,
										&serviceBufferCount,
										BLUETOOTH_GATT_FLAG_NONE)) != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
	{
		PythonPrintError((PCHAR)"BluetoothGATTGetServices Error");
		goto DONE;
	}

	pServiceBuffer = (PBTH_LE_GATT_SERVICE)malloc(sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);

	if (pServiceBuffer == NULL)
	{
		PythonPrintError((PCHAR)"pServiceBuffer out of memory");
		goto DONE;
	}
	else
	{
		RtlZeroMemory(pServiceBuffer, sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);
	}

	if((resSvc = BluetoothGATTGetServices(hLEDevice,
										serviceBufferCount,
										pServiceBuffer,
										&numServices,
										BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
	{
		PythonPrintError((PCHAR)"BluetoothGATTGetServices error");
		goto DONE;
	}

	StringFromCLSID(pServiceBuffer->ServiceUuid.Value.LongUuid, &pStr);

	if((resCh = BluetoothGATTGetCharacteristics(hLEDevice,
												pServiceBuffer,
												0,
												NULL,
												&charBufferSize,
												BLUETOOTH_GATT_FLAG_NONE)) != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
	{
		PythonPrintError((PCHAR)"BluetoothGATTGetCharacteristics Error");
		goto DONE;
	}

	if (charBufferSize > 0)
	{
		pCharBuffer = (PBTH_LE_GATT_CHARACTERISTIC)malloc(charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));

		if (pCharBuffer == NULL)
		{
			PythonPrintError((PCHAR)"pCharBuffer out of memory");
			goto DONE;
		}
		else
		{
			RtlZeroMemory(pCharBuffer, charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));
		}

		if((resCh = BluetoothGATTGetCharacteristics(hLEDevice,
													pServiceBuffer,
													charBufferSize,
													pCharBuffer,
													&numChars,
													BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
		{
			PythonPrintError((PCHAR)"BluetoothGATTGetCharacteristics Error");
			goto DONE;
		}
	}

	PBTH_LE_GATT_CHARACTERISTIC currGattChar;
	USHORT descriptorBufferSize;

	for (int ii = 0; ii < charBufferSize; ii++)
	{
		currGattChar = &pCharBuffer[ii];
		StringFromCLSID(currGattChar->CharacteristicUuid.Value.LongUuid, &pStr);

		int z = 0;

		//  loop through all of the UUIDs we are interested in getting data from
		while(uuids[z] != NULL)
		{
			if ((_wcsicmp(pStr, uuids[z])) == 0)
			{
				if ((resGD = BluetoothGATTGetDescriptors(hLEDevice,
					currGattChar,
					0,
					NULL,
					&descriptorBufferSize,
					BLUETOOTH_GATT_FLAG_NONE)) != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
				{
					PythonPrintError((PCHAR)"BluetoothGATTGetDescriptors Error");
				}

				PBTH_LE_GATT_DESCRIPTOR pDescriptorBuffer;
				if (descriptorBufferSize > 0)
				{
					pDescriptorBuffer = (PBTH_LE_GATT_DESCRIPTOR)malloc(descriptorBufferSize *
						sizeof(BTH_LE_GATT_DESCRIPTOR));

					if (pDescriptorBuffer == NULL)
					{
						PythonPrintError((PCHAR)"DescriptorBuffer out of memory Error");
						goto DONE;
					}
					else
					{
						RtlZeroMemory(pDescriptorBuffer, descriptorBufferSize);
					}

					////////////////////////////////////////////////////////////////////////////
					// Retrieve Descriptors
					////////////////////////////////////////////////////////////////////////////

					USHORT numDescriptors;
					if ((resGD = BluetoothGATTGetDescriptors(hLEDevice,
						currGattChar,
						descriptorBufferSize,
						pDescriptorBuffer,
						&numDescriptors,
						BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
					{
						PythonPrintError((PCHAR)"BluetoothGATTGetDescriptors Error #2");
						goto DONE;
					}

					for (int kk = 0; kk < numDescriptors; kk++)
					{
						PBTH_LE_GATT_DESCRIPTOR  currGattDescriptor = &pDescriptorBuffer[kk];
						////////////////////////////////////////////////////////////////////////////
						// Determine Descriptor Value Buffer Size
						////////////////////////////////////////////////////////////////////////////
						USHORT descValueDataSize;
						if ((resGDV = BluetoothGATTGetDescriptorValue(hLEDevice,
							currGattDescriptor,
							0,
							NULL,
							&descValueDataSize,
							BLUETOOTH_GATT_FLAG_NONE)) != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
						{
							PythonPrintError((PCHAR)"BluetoothGATTGetDescriptorValue Error");
						}

						PBTH_LE_GATT_DESCRIPTOR_VALUE pDescValueBuffer = (PBTH_LE_GATT_DESCRIPTOR_VALUE)malloc(descValueDataSize);

						if (pDescValueBuffer == NULL)
						{
							PythonPrintError((PCHAR)"DescValueBuffer Error");
						}
						else
						{
							RtlZeroMemory(pDescValueBuffer, descValueDataSize);
						}

						////////////////////////////////////////////////////////////////////////////
						// Retrieve the Descriptor Value
						////////////////////////////////////////////////////////////////////////////

						if ((resGDV = BluetoothGATTGetDescriptorValue(hLEDevice,
							currGattDescriptor,
							(ULONG)descValueDataSize,
							pDescValueBuffer,
							NULL,
							BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
						{
							PythonPrintError((PCHAR)"BluetoothGATTGetDescriptorsValue Error #2");
						}

						BTH_LE_GATT_DESCRIPTOR_VALUE newValue;
						RtlZeroMemory(&newValue, sizeof(newValue));
						newValue.DescriptorType = ClientCharacteristicConfiguration;
						newValue.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;

						if ((resSDV = BluetoothGATTSetDescriptorValue(hLEDevice,
							currGattDescriptor,
							&newValue,
							BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
						{
							PythonPrintError((PCHAR)"BluetoothGATTGetDescriptorValue Error #3");
						}
						else
						{
//							std::cout << "\t\tSetting desciptor notification for service handle: " << currGattDescriptor->ServiceHandle << std::endl;
						}
					}
				}
			}

			z++;
		}

		BLUETOOTH_GATT_EVENT_HANDLE EventHandle;

		if (currGattChar->IsNotifiable)
		{
			BTH_LE_GATT_EVENT_TYPE EventType = CharacteristicValueChangedEvent;

			BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION EventParameterIn;
			EventParameterIn.Characteristics[0] = *currGattChar;
			EventParameterIn.NumCharacteristics = 1;

			if((resRE = BluetoothGATTRegisterEvent(hLEDevice,
												EventType,
												&EventParameterIn,
												(PFNBLUETOOTH_GATT_EVENT_CALLBACK)ProcessEvent,
												NULL,
												&EventHandle,
												BLUETOOTH_GATT_FLAG_NONE)) != S_OK)
			{
				PythonPrintError((PCHAR)"BluetoothGATTRegisterEvent Error");
			}

			BTH_LE_GATT_CHARACTERISTIC_VALUE newValue;

			RtlZeroMemory(&newValue, (sizeof(newValue)));

			newValue.DataSize = sizeof(ULONG);
			newValue.Data[0] = 0x100;

			// Set the new characteristic value
			if((resSCV = BluetoothGATTSetCharacteristicValue(hLEDevice,
															currGattChar,
															&newValue,
															NULL,
															BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE)) != S_OK)
			{
				PythonPrintError((PCHAR)"BluetoothGATTSetCharacteristicValue Error");
			}
		}
	}

DONE:
	;
}


/*
FUNCTION:	GetBLEHandle
PURPOSE:	Find the device we are trying to connect to and open a handle to the specific device
*/
HANDLE GetBLEHandle(__in GUID AGuid)
{
	HDEVINFO hDI;
	SP_DEVICE_INTERFACE_DATA did;
	SP_DEVINFO_DATA dd;
	GUID BluetoothInterfaceGUID = AGuid;
	HANDLE hComm = NULL;

	if((hDI = SetupDiGetClassDevs(&BluetoothInterfaceGUID,
		NULL,
		NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)) == INVALID_HANDLE_VALUE)
	{
		PrintError((LPWSTR)TEXT("SetupDiGetClassDevs"));
		PythonPrintError((PCHAR)"Error with SetupDiGetClassDevs");
		return (HANDLE)NULL;
	}

	did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	dd.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDI, NULL, &BluetoothInterfaceGUID, i, &did); i++)
	{
		SP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData;

		DeviceInterfaceDetailData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		DWORD size = 0;
		DWORD dwErr = 0;

		if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, NULL, 0, &size, 0))
		{
			PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)GlobalAlloc(GPTR, size);

			pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, pInterfaceDetailData, size, &size, &dd))
			{
				dwErr = GetLastError();
				if (dwErr != ERROR_NO_MORE_ITEMS)
				{
					PythonPrintError((PCHAR)"Error with SetupDiGetDeviceInterfaceDetail (#2)");
					return (HANDLE)NULL;
				}				
			}
			else
			{
				if ((GetBluetoothDeviceID(hDI, dd)) != ERROR_SUCCESS)
					PythonPrintError((PCHAR)"Error with GetBluetoothDeviceID");

				hComm = CreateFile(
					pInterfaceDetailData->DevicePath,
					GENERIC_WRITE | GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);

				GlobalFree(pInterfaceDetailData);
			}
		}
	}

	SetupDiDestroyDeviceInfoList(hDI);
	return hComm;
}


/*
	FUNCTION:	btle_init
	PURPOSE:	open a handle to the bluetooth device we want to communicate with
*/
HANDLE btle_init(WCHAR *wchDevice)
{
	HANDLE hLEDevice = NULL;
	GUID AGuid;

	CLSIDFromString(wchDevice, &AGuid);

	if ((hLEDevice = GetBLEHandle(AGuid)) == NULL)
	{
		PythonPrintError((PCHAR)"Handle Error");
		return (HANDLE)-1;
	}

	return hLEDevice;
}


/*
	FUNCTION:	btle_disconnect
	PURPOSE:	close the handle to the bluetooth device
*/
void btle_disconnect(HANDLE hComm)
{
	CloseHandle(hComm);
}