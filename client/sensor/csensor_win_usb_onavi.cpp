/*
 *  csensor_win_usb_onavi01.cpp
 *  qcn
 *
 *  Created by Carl Christensen on 10/11/2009.
 *  Copyright 2009 Stanford University.  All rights reserved.
 *
 * Implementation file for Windows USB ONavi 1 sensor class
 */

#include "main.h"
#include "csensor_win_usb_onavi.h"

// enum serial port
// The next 3 includes are needed for serial port enumeration
#include <objbase.h>
#include <initguid.h>
#include <Setupapi.h>

// The following define is from ntddser.h in the DDK. It is also
// needed for serial port enumeration.
#ifndef GUID_CLASS_COMPORT
DEFINE_GUID(GUID_CLASS_COMPORT, 0x86e0d1e0L, 0x8089, 0x11d0, 0x9c, 0xe4, \
			0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73);
#endif

//{4D36E978-E325-11CE-BFC1-08 00 2B E1 03 18}
DEFINE_GUID(GUID_CLASS_ONAVI_1, 0x4d36e978L, 0xE325, 0x11CE, 0xbf, 0xc1, \
			0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);


//---------------------------------------------------------------
// Helpers for enumerating the available serial ports.
// These throw a std::string on failure, describing the nature of
// the error that occurred.

void EnumPortsWdm(std::vector<SSerInfo> &asi);
//void EnumPortsWNt4(std::vector<SSerInfo> &asi);
//void EnumPortsW9x(std::vector<SSerInfo> &asi);
//void SearchPnpKeyW9x(HKEY hkPnp, bool bUsbDevice,
//					 std::vector<SSerInfo> &asi);



CSensorWinUSBONavi::CSensorWinUSBONavi()
  : CSensor(), m_hcom(INVALID_HANDLE_VALUE), m_usBitSensor(0)
{ 
   memset(&m_si, 0x00, sizeof(SSerInfo));
}

CSensorWinUSBONavi::~CSensorWinUSBONavi()
{
  closePort();
}

void CSensorWinUSBONavi::closePort()
{
	if (m_hcom != INVALID_HANDLE_VALUE) { // handle exists
		::CloseHandle(m_hcom);
	}
	m_hcom = INVALID_HANDLE_VALUE;
    memset(&m_si, 0x00, sizeof(SSerInfo));
	setPort();
	setType();
}

bool CSensorWinUSBONavi::detect()
{
	bool bRet = false;
	if (SearchONaviSerialPort(m_si)) {
		// open & validate the virtual com port persistent
			char *strPort = new char[_MAX_PATH];
			memset(strPort, 0x00, sizeof(char) * _MAX_PATH);
			//sprintf(strPort, "\\\\.\\COM%d", m_si.iCOM);
			strcpy(strPort, m_si.strDevPath.c_str());
			m_hcom = ::CreateFile((LPCSTR) strPort,
				GENERIC_READ | GENERIC_WRITE,
				0x00,    /* comm devices must be opened w/exclusive-access */
				NULL, /* no security attrs */
				OPEN_EXISTING, /* comm devices must use OPEN_EXISTING */
				FILE_ATTRIBUTE_NORMAL,    /* not overlapped I/O */
				NULL  /* hTemplate must be NULL for comm devices */		);

			delete [] strPort;
			if (m_hcom == INVALID_HANDLE_VALUE) { // fail
				closePort();
			}
			else {
				// setup communications parameters
				DCB dcb;
				if (::GetCommState(m_hcom, &dcb)) {
					dcb.DCBlength = sizeof(DCB);
					dcb.BaudRate = CBR_115200;
					dcb.ByteSize = 8;
					dcb.Parity = NOPARITY;
					dcb.StopBits = ONESTOPBIT;
					dcb.fBinary = TRUE;
					dcb.fParity = FALSE;

					dcb.fOutxCtsFlow = FALSE;
					dcb.fOutxDsrFlow = FALSE;

					dcb.fDtrControl = DTR_CONTROL_ENABLE;
					dcb.fRtsControl = RTS_CONTROL_ENABLE;

					dcb.fDsrSensitivity = FALSE;
					dcb.fTXContinueOnXoff = FALSE;
					dcb.fOutX = FALSE;
					dcb.fInX = FALSE;
					dcb.fErrorChar = FALSE;
					dcb.fNull = FALSE;
					dcb.fAbortOnError = FALSE;

					dcb.XonChar = 17;
					dcb.XoffChar = 19;
			

					if (::SetCommState(m_hcom, &dcb)) { // successfully set the state
						setSingleSampleDT(true); 
						// try to read a value and get the sensor bit-type (& hence sensor type)
						float x,y,z;
						m_usBitSensor = 0;
						setPort(m_si.iCOM); // just temporary so the read_xyz works
						if (read_xyz(x,y,z) && m_usBitSensor > 0) {
						   // exists, so setPort & Type
						   switch(m_usBitSensor) {
							 case 12:
								 setType(SENSOR_USB_ONAVI_A_12);
									setPort(m_usBitSensor);
								bRet = true;
								 break;
							 case 16:
								 setType(SENSOR_USB_ONAVI_B_16);
								setPort(m_usBitSensor);
								bRet = true;
				                 break;
							 case 24:
								 setType(SENSOR_USB_ONAVI_C_24);
								setPort(m_usBitSensor);
								bRet = true;
								 break;
							 default: // error!
							   closePort();
							   bRet = false;
						   }
						}
						else {
						   closePort();
						   bRet = false;
						} // read_xyz test
					}  // set com state
				} // m_hcom
				else { // fail
					closePort();
                    bRet = false;
				}  // com settings
			}  // m_hcom valid
	} // search onavi port
	return bRet;
}

inline bool CSensorWinUSBONavi::read_xyz(float& x1, float& y1, float& z1)
{
	/*
We tried to keep the data as simple as possible. The data looks like: 

##xxyyzzs 

Two ASCII '#' (x23) followed by the X value upper byte, X value lower byte, Y value upper byte, Y value lower byte, Z value upper byte, Z value lower byte and an eight bit checksum.  

The bytes are tightly packed and there is nothing between the data values except for the sentence start ##.  

The X and Y axis will have a 0g offset at 32768d (x8000) and the Z axis offset at +1g 45874d (xB332) when oriented in the X/Y horizontal and Z up position.  The ‘s’ value is a one byte checksum.  

It is a sum of all of the bytes, truncated to the lower byte.  This firmware does not transmit the temperature value. 

Finding g as a value:

g  = x - 32768 * (5 / 65536) 

Where: x is the data value 0 - 65536 (x0000 to xFFFF). 

Values >32768 are positive g and <32768 are negative g. The sampling rate is set to 200Hz, and the analog low-pass filters are set to 50Hz.  The data is oversampled 2X over Nyquist. We are going to make a new version of the module, with 25Hz LP analog filters and dual sensitivity 2g / 6g shortly.  Same drivers, same interface.  I’ll get you one as soon as I we get feedback on this and make a set of those.

##?҅?M??##?⅑M??##?ׅ?M??##?ȅ{Mʥ##?Ѕ?M??##?Ʌ?M??##?݅?M??##?ԅ?M??##?҅{M??
	*/

     static float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f; // keep last values

	// first check for valid port
	if (getPort() < 0 || m_hcom == INVALID_HANDLE_VALUE) return false;

	DWORD dwRead = 0L;

	const int ciLen = 9;
	QCN_BYTE bytesIn[ciLen+1], cs;
	const char cWrite = '*';
	int x, y, z;
	int iCS = 0;
	// return values, note BOOL for Windows function calls
	bool bRet = false;
	BOOL BRet = FALSE;

	x = y = z = x1 = y1 = z1 = cs = 0x00;
    x1 = x0; y1 = y0; z1 = z0;  // use last good values


	BRet = ::WriteFile(m_hcom, &cWrite, 1, &dwRead, NULL) && ::FlushFileBuffers(m_hcom);
	if (BRet && dwRead == 1) {
		dwRead = 0;
		memset(bytesIn, 0x00, ciLen+1);
		//if (::SetFilePointer(m_hcom, -ciLen, NULL, FILE_END) && ::ReadFile(m_hcom, bytesIn, ciLen, &dwRead, NULL) && dwRead == ciLen) {

		BRet = ::SetFilePointer(m_hcom, 0, NULL, FILE_BEGIN);
		if (BRet == INVALID_SET_FILE_POINTER) // Test for failure
	    { 
		  // Obtain the error code. 
			DWORD dwError = GetLastError() ; 
			BRet = FALSE;
	        bRet = false;
	    } // End of error handler 
		else {
			BRet = ::ReadFile((HANDLE) m_hcom, (LPVOID) bytesIn, (DWORD) ciLen, (LPDWORD) &dwRead, NULL);
		}
		if (BRet && dwRead == ciLen) {
			// good data length read in, now test for appropriate characters
			if (bytesIn[ciLen] == 0x00) { // && bytesIn[0] == 0x23 && bytesIn[1] == 0x23) {
					// format is ##XXYYZZC\0
					// we found both, the bytes in between are what we want (really bytes after lOffset[0]
					if (m_usBitSensor == 0) { // need to find sensor bit type i.e. 12/16/24-bit ONavi
					   if (bytesIn[0] == 0x2A && bytesIn[1] == 0x2A) {  // **
						  m_usBitSensor = 12;
					   }
					   else if (bytesIn[0] == 0x23 && bytesIn[1] == 0x23) { // ##
						  m_usBitSensor = 16;
					   }
					   else if (bytesIn[0] == 0x24 && bytesIn[1] == 0x24) {  // $$
						  m_usBitSensor = 24;
					   }
					}
					// we found both, the bytes in between are what we want (really bytes after lOffset[0]
					x = (bytesIn[2] * 256) + bytesIn[3];
					y = (bytesIn[4] * 256) + bytesIn[5];
					z = (bytesIn[6] * 256) + bytesIn[7];
					cs   = bytesIn[8];
					for (int i = 2; i <= 7; i++) iCS += bytesIn[i];

					// convert to g decimal value
					// g  = x - 32768 * (5 / 65536) 
					// Where: x is the data value 0 - 65536 (x0000 to xFFFF). 

	#ifdef QCN_RAW_DATA
					x1 = (float) x;
					y1 = (float) y;
					z1 = (float) z;
	#else           
					x1 = ((float) x - 32768.0f) * FLOAT_ONAVI_FACTOR * EARTH_G;
					y1 = ((float) y - 32768.0f) * FLOAT_ONAVI_FACTOR * EARTH_G;
					z1 = ((float) z - 32768.0f) * FLOAT_ONAVI_FACTOR * EARTH_G;
	#endif 
					
					x0 = x1; y0 = y1; z0 = z1;  // preserve values
					bRet = true;
				}
				else {
					bRet = false;
									fprintf(stderr, "%f: ONavi Error in read_xyz() - not null terminated string\n", sm->t0active);
									fflush(stderr);
				}
			}
			else {
                                fprintf(stderr, "%f: ONavi Error in read_xyz() - SetFilePointer returned False\n", sm->t0active);
                                fflush(stderr);
			bRet = false;
		}
	}
	else {
                                fprintf(stderr, "%f: ONavi Error in read_xyz() - WriteFile returned False\n", sm->t0active);
                                fflush(stderr);
		bRet = false;
	}

	return bRet;
}


//---------------------------------------------------------------
// Routine for enumerating the available serial ports.
// Throws a std::string on failure, describing the error that
// occurred. If bIgnoreBusyPorts is true, ports that can't
// be opened for read/write access are not included.

bool CSensorWinUSBONavi::SearchONaviSerialPort(SSerInfo& si, const bool bIgnoreBusyPorts)
{
	bool bRet = false;
	memset(&si, 0x00, sizeof(SSerInfo)); 
	vector<SSerInfo> asi;

	// Use different techniques to enumerate the available serial
	// ports, depending on the OS we're using
	OSVERSIONINFO vi;
	vi.dwOSVersionInfoSize = sizeof(vi);
	/*
	if (!::GetVersionEx(&vi)) {
		std::string str = "Could not get OS version." ;
		throw str;
	}
	// Handle windows 9x and NT4 specially
	if (vi.dwMajorVersion < 5) {
		if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT)
			EnumPortsWNt4(asi);
		else
			EnumPortsW9x(asi);
	}
	else {
		*/
		// Win2k and later support a standard API for
		// enumerating hardware devices.
	try {
		EnumPortsWdm(asi);
	}
	catch(std::string strCatchErr) {
		fprintf(stderr, "ONavi Err: %s\n", strCatchErr.c_str());
		return false;
	}

	//}

	std::vector<SSerInfo>::iterator iSI;   // iterator for a SSerInfo vector
	for (iSI=asi.begin(); iSI != asi.end(); iSI++)
	{
		if (bIgnoreBusyPorts) {
			// Only display ports that can be opened for read/write
			HANDLE hCom = ::CreateFile((LPCSTR) iSI->strDevPath.c_str(),
				GENERIC_READ,
				0x00,    /* comm devices must be opened w/exclusive-access */
				NULL, /* no security attrs */
				OPEN_EXISTING, /* comm devices must use OPEN_EXISTING */
				FILE_ATTRIBUTE_NORMAL,    /* not overlapped I/O */
				NULL  /* hTemplate must be NULL for comm devices */				);
			if (hCom == INVALID_HANDLE_VALUE) {
				// It can't be opened; remove it.
				//asi.erase(iSI); // no need to erase
				continue;
			}
			else {
				// It can be opened! Close it and add it to the list
				::CloseHandle(hCom);
			}
		}

		// Come up with a name for the device.
		// If there is no friendly name, use the port name.
		if (iSI->strFriendlyName.empty())
			iSI->strFriendlyName = iSI->strPortName;

		// If there is no description, try to make one up from
		// the friendly name.
		if (iSI->strPortDesc.empty()) {
			// If the port name is of the form "ACME Port (COM3)"
			// then strip off the " (COM3)"
			iSI->strPortDesc = iSI->strFriendlyName;
			int startdex = (int) iSI->strPortDesc.find(" (");
			int enddex = (int) iSI->strPortDesc.find(")");
			if (startdex > 0 && enddex == 
				(iSI->strPortDesc.length()-1))
				iSI->strPortDesc = iSI->strPortDesc.substr(0,startdex);
		}
		if (iSI->strFriendlyName.find(STR_USB_ONAVI01) == 0) {
			si = *iSI;
			bRet = true;
		}

	} // for loop
	return bRet;
}

// Helpers for EnumSerialPorts

void EnumPortsWdm(std::vector<SSerInfo> &asi)
{
	std::string strErr;
	// Create a device information set that will be the container for 
	// the device interfaces.
	GUID *guidDev = (GUID*) &GUID_CLASS_ONAVI_1; //&GUID_CLASS_COMPORT;

	HDEVINFO hDevInfo = INVALID_HANDLE_VALUE;
	SP_DEVICE_INTERFACE_DETAIL_DATA *pDetData = NULL;

	try {
		hDevInfo = ::SetupDiGetClassDevs( guidDev,
			NULL,
			NULL,
			DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
			
		if(hDevInfo == INVALID_HANDLE_VALUE) 
		{
			strErr = "SetupDiGetClassDevs failed. (err=%lx)";
			throw strErr;
		}

		// Enumerate the serial ports
		bool bOk = false;
		SP_DEVICE_INTERFACE_DATA ifcData;
		DWORD dwDetDataSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA) + 256;
		pDetData = (SP_DEVICE_INTERFACE_DETAIL_DATA*) new char[dwDetDataSize];
		// This is required, according to the documentation. Yes,
		// it's weird.
		ifcData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
		pDetData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		int ii = 0;
		while (! bOk) {
			bOk = (bool) ::SetupDiEnumDeviceInterfaces(hDevInfo,
				NULL, guidDev, ii, &ifcData);
			if (bOk) {
				// Got a device. Get the details.
				SP_DEVINFO_DATA devdata = {sizeof(SP_DEVINFO_DATA)};
				bOk = (bool) ::SetupDiGetDeviceInterfaceDetail(hDevInfo,
					&ifcData, pDetData, dwDetDataSize, NULL, &devdata);
				if (bOk) {
					std::string strDevPath(pDetData->DevicePath);
					// Got a path to the device. Try to get some more info.
					TCHAR fname[256];
					TCHAR desc[256];
					bool bSuccess = (bool) ::SetupDiGetDeviceRegistryProperty(
						hDevInfo, &devdata, SPDRP_FRIENDLYNAME, NULL,
						(PBYTE)fname, sizeof(fname), NULL);
					bSuccess = bSuccess && ::SetupDiGetDeviceRegistryProperty(
						hDevInfo, &devdata, SPDRP_DEVICEDESC, NULL,
						(PBYTE)desc, sizeof(desc), NULL);

					if (bSuccess) {
						// Add an entry to the array
						SSerInfo si;
						si.strDevPath = strDevPath;
						si.strFriendlyName = fname;
						si.strPortDesc = desc;
						si.bUsbDevice = (bool)(strstr(fname, "USB")!=NULL);
						char* strCOM = strstr(fname, "COM");
						if (strCOM && strlen(strCOM)>3) {  // COM port in the string
							si.iCOM = atoi(strCOM+3);
						}
						asi.push_back(si);
					}
				}
				else {
					strErr = "SetupDiGetDeviceInterfaceDetail failed. (err=%lx)";
					throw strErr;
				}
			}
			else {
				DWORD err = GetLastError();
				if (err == ERROR_NO_MORE_ITEMS) {
					break;
				}
				else {
					strErr = "SetupDiEnumDeviceInterfaces failed. (err=%lx)";
					throw strErr;
				}
			}
            bOk = false;  // reset to try another USB port
			ii++; // increment device counter
		}
	}
	catch (std::string strCatchErr) {
		strErr = strCatchErr;
	}

	if (pDetData != NULL)
		delete [] (char*)pDetData;
	if (hDevInfo != INVALID_HANDLE_VALUE)
		::SetupDiDestroyDeviceInfoList(hDevInfo);

	if (!strErr.empty())
		throw strErr;
}

/*
void EnumPortsWNt4(std::vector<SSerInfo> &asi)
{
	// NT4's driver model is totally different, and not that
	// many people use NT4 anymore. Just try all the COM ports
	// between 1 and 16
	SSerInfo si;
	for (int ii=1; ii<=16; ii++) {
		char strTmp[16];
		sprintf(strTmp, "COM%d", ii);
		std::string strPort = strTmp;
		si.strDevPath = std::string("\\\\.\\") + strPort;
		si.strPortName = strPort;
		asi.push_back(si);
	}
}

void EnumPortsW9x(std::vector<SSerInfo> &asi)
{
	// Look at all keys in HKLM\Enum, searching for subkeys named
	// *PNP0500 and *PNP0501. Within these subkeys, search for
	// sub-subkeys containing value entries with the name "PORTNAME"
	// Search all subkeys of HKLM\Enum\USBPORTS for PORTNAME entries.

	// First, open HKLM\Enum
	HKEY hkEnum = NULL;
	HKEY hkSubEnum = NULL;
	HKEY hkSubSubEnum = NULL;

	try {
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ,
			&hkEnum) != ERROR_SUCCESS)
			throw std::string("Could not read from HKLM\\Enum");

		// Enumerate the subkeys of HKLM\Enum
		char acSubEnum[128];
		DWORD dwSubEnumIndex = 0;
		DWORD dwSize = sizeof(acSubEnum);
		while (RegEnumKeyEx(hkEnum, dwSubEnumIndex++, acSubEnum, &dwSize,
			NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
		{
			HKEY hkSubEnum = NULL;
			if (RegOpenKeyEx(hkEnum, acSubEnum, 0, KEY_READ,
				&hkSubEnum) != ERROR_SUCCESS)
				throw std::string("Could not read from HKLM\\Enum\\")+acSubEnum;

			// Enumerate the subkeys of HKLM\Enum\*\, looking for keys
			// named *PNP0500 and *PNP0501 (or anything in USBPORTS)
			bool bUsbDevice = (strcmp(acSubEnum,"USBPORTS")==0);
			char acSubSubEnum[128];
			dwSize = sizeof(acSubSubEnum);  // set the buffer size
			DWORD dwSubSubEnumIndex = 0;
			while (RegEnumKeyEx(hkSubEnum, dwSubSubEnumIndex++, acSubSubEnum,
				&dwSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
			{
				bool bMatch = (strcmp(acSubSubEnum,"*PNP0500")==0 ||
					strcmp(acSubSubEnum,"*PNP0501")==0 ||
					bUsbDevice);
				if (bMatch) {
					HKEY hkSubSubEnum = NULL;
					if (RegOpenKeyEx(hkSubEnum, acSubSubEnum, 0, KEY_READ,
						&hkSubSubEnum) != ERROR_SUCCESS)
						throw std::string("Could not read from HKLM\\Enum\\") + 
						acSubEnum + "\\" + acSubSubEnum;
					SearchPnpKeyW9x(hkSubSubEnum, bUsbDevice, asi);
					RegCloseKey(hkSubSubEnum);
					hkSubSubEnum = NULL;
				}

				dwSize = sizeof(acSubSubEnum);  // restore the buffer size
			}

			RegCloseKey(hkSubEnum);
			hkSubEnum = NULL;
			dwSize = sizeof(acSubEnum); // restore the buffer size
		}
	}
	catch (std::string strError) {
		if (hkEnum != NULL)
			RegCloseKey(hkEnum);
		if (hkSubEnum != NULL)
			RegCloseKey(hkSubEnum);
		if (hkSubSubEnum != NULL)
			RegCloseKey(hkSubSubEnum);
		throw strError;
	}

	RegCloseKey(hkEnum);
}

void SearchPnpKeyW9x(HKEY hkPnp, bool bUsbDevice,
					 std::vector<SSerInfo> &asi)
{
	// Enumerate the subkeys of the given PNP key, looking for values with
	// the name "PORTNAME"
	// First, open HKLM\Enum
	HKEY hkSubPnp = NULL;

	try {
		// Enumerate the subkeys of HKLM\Enum\*\PNP050[01]
		char acSubPnp[128];
		DWORD dwSubPnpIndex = 0;
		DWORD dwSize = sizeof(acSubPnp);
		while (RegEnumKeyEx(hkPnp, dwSubPnpIndex++, acSubPnp, &dwSize,
			NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
		{
			HKEY hkSubPnp = NULL;
			if (RegOpenKeyEx(hkPnp, acSubPnp, 0, KEY_READ,
				&hkSubPnp) != ERROR_SUCCESS)
				throw std::string("Could not read from HKLM\\Enum\\...\\")
				+ acSubPnp;

			// Look for the PORTNAME value
			char acValue[128];
			dwSize = sizeof(acValue);
			if (RegQueryValueEx(hkSubPnp, "PORTNAME", NULL, NULL, (BYTE*)acValue,
				&dwSize) == ERROR_SUCCESS)
			{
				std::string strPortName(acValue);

				// Got the portname value. Look for a friendly name.
				std::string strFriendlyName;
				dwSize = sizeof(acValue);
				if (RegQueryValueEx(hkSubPnp, "FRIENDLYNAME", NULL, NULL, (BYTE*)acValue,
					&dwSize) == ERROR_SUCCESS)
					strFriendlyName = acValue;

				// Prepare an entry for the output array.
				SSerInfo si;
				si.strDevPath = std::string("\\\\.\\") + strPortName;
				si.strPortName = strPortName;
				si.strFriendlyName = strFriendlyName;
				si.bUsbDevice = bUsbDevice;

				// Overwrite duplicates.
				bool bDup = false;
				for (int ii=0; ii<asi.size() && !bDup; ii++)
				{
					if (asi[ii].strPortName == strPortName) {
						bDup = true;
						asi[ii] = si;
					}
				}
				if (!bDup) {
					// Add an entry to the array
					asi.push_back(si);
				}
			}

			RegCloseKey(hkSubPnp);
			hkSubPnp = NULL;
			dwSize = sizeof(acSubPnp);  // restore the buffer size
		}
	}
	catch (std::string strError) {
		if (hkSubPnp != NULL)
			RegCloseKey(hkSubPnp);
		throw strError;
	}
}

*/

