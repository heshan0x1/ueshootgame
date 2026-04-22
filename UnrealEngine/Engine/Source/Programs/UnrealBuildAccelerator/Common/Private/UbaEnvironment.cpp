// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaEnvironment.h"
#include "UbaStringBuffer.h"

#if PLATFORM_WINDOWS
#include <powerbase.h>
#pragma comment(lib, "Powrprof.lib")
#elif PLATFORM_MAC
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#if PLATFORM_WINDOWS
typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG  Number;
    ULONG  MaxMhz;
    ULONG  CurrentMhz;
    ULONG  MhzLimit;
    ULONG  MaxIdleState;
    ULONG  CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;
#endif

namespace uba
{
	MutexHandle CreateMutexW(bool bInitialOwner, const tchar* lpName)
	{
		#if PLATFORM_WINDOWS
		return (MutexHandle)(u64)::CreateMutexW(NULL, bInitialOwner, lpName);
		#else
		// TODO: This is used to check for exclusivity and also for trace streams (only created by host and read by visualizer)
		SetLastError(ERROR_SUCCESS);
		return ((MutexHandle)(u64)1337); // Just some random value
		#endif
	}

	void ReleaseMutex(MutexHandle mutex)
	{
		if (mutex == InvalidMutexHandle)
			return;
		#if PLATFORM_WINDOWS
		::ReleaseMutex((HANDLE)mutex);
		#else
		#endif
	}

	void CloseMutex(MutexHandle mutex)
	{
		if (mutex == InvalidMutexHandle)
			return;
		#if PLATFORM_WINDOWS
		::CloseHandle((HANDLE)mutex);
		#else
		#endif
	}

	u32 GetEnvironmentVariableW(const tchar* name, tchar* buffer, u32 nSize)
	{
		#if PLATFORM_WINDOWS
		return ::GetEnvironmentVariableW(name, buffer, nSize);
		#else
		const char* env = getenv(name);
		if (!env)
		{
			SetLastError(203); // ERROR_ENVVAR_NOT_FOUND
			return 0;
		}

		auto envLen = strlen(env);
		if (nSize <= envLen)
			return envLen + 1;
		memcpy(buffer, env, envLen + 1);
		return envLen;
		#endif
	}

	bool SetEnvironmentVariableW(const tchar* name, const tchar* value)
	{
		#if PLATFORM_WINDOWS
		return ::SetEnvironmentVariableW(name, value);
		#else
		return setenv(name, value, 1) == 0;
		#endif
	}

	u32 ExpandEnvironmentStringsW(const tchar* lpSrc, tchar* lpDst, u32 nSize)
	{
		#if PLATFORM_WINDOWS
		return ::ExpandEnvironmentStringsW(lpSrc, lpDst, nSize);
		#else
		UBA_ASSERTF(false, TC("ExpandEnvironmentStringsW not implemented (%s)"), lpSrc);
		return 0;
		#endif
	}

	ProcHandle GetCurrentProcessHandle()
	{
		#if PLATFORM_WINDOWS
		return (ProcHandle)(u64)GetCurrentProcess();
		#else
		UBA_ASSERTF(false, TC("GetCurrentProcessHandle not implemented"));
		return (ProcHandle)0;
		#endif
	}

	u32 GetLogicalProcessorCount()
	{
		#if PLATFORM_WINDOWS
		return ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		#else
		return (u32)sysconf(_SC_NPROCESSORS_ONLN);
		#endif
	}

	u32 GetProcessorGroupCount()
	{
		#if PLATFORM_WINDOWS
		static u32 s_processorGroupCount = u32(GetActiveProcessorGroupCount());
		if (s_processorGroupCount)
			return s_processorGroupCount;
		#endif
		return 1u;
	}

	void ElevateCurrentThreadPriority()
	{
		#if PLATFORM_WINDOWS
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		#endif
	}

#if PLATFORM_WINDOWS
	u64 GetMaxPageFileSize(Logger& logger, MEMORYSTATUSEX& memStatus)
	{
		tchar systemDrive = 'c';
		{
			tchar temp[32];
			if (GetEnvironmentVariableW(TC("SystemDrive"), temp, 32))
				systemDrive = ToLower(temp[0]);
		}

		u64 maxPageSize = 0;
		wchar_t str[1024];
		DWORD strBytes = sizeof(str);
		LSTATUS res = RegGetValueW(HKEY_LOCAL_MACHINE, TC("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management"), TC("PagingFiles"), RRF_RT_REG_MULTI_SZ, NULL, str, &strBytes);
		if (res == ERROR_SUCCESS)
		{
			DWORD pagefileOnOsVolume = 0;
			DWORD pagefileOnOsVolumeSize = 4;
			RegGetValueW(HKEY_LOCAL_MACHINE, TC("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management"), TC("PagefileOnOsVolume"), RRF_RT_DWORD, NULL, &pagefileOnOsVolume, &pagefileOnOsVolumeSize);

			wchar_t* line = str;
			for (; size_t lineLen = wcslen(line); line += lineLen + 1)
			{
				if (lineLen < 3)
					continue;

				u64 maxSizeMb = 0;

				StringBuffer<8> drive;
				drive.Append(line, 3); // Get drive root path

				if (drive[0] == '?') // Drive '?' can exist when "Automatically manage paging file size for all drives".. 
				{
					// We can use ExistingPageFiles registry key to figure out which drive...
					// This key can contain multiple page files normally.. have no idea if it can contain multiple when drive is '?'.. but for now, just use the first
					wchar_t str2[1024];
					DWORD str2Bytes = sizeof(str2);
					res = RegGetValueW(HKEY_LOCAL_MACHINE, TC("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management"), TC("ExistingPageFiles"), RRF_RT_REG_MULTI_SZ, NULL, str2, &str2Bytes);
					if (res != ERROR_SUCCESS)
						continue;

					auto colon = wcschr(str2, ':'); // Path is something like \??\C:\pagefile.sys or similar.. let's search for : and use character in front of it.
					if (colon == nullptr || colon == str2)
						continue;

					drive[0] = colon[-1];

					if (pagefileOnOsVolume && ToLower(drive[0]) != systemDrive)
						continue;
				}
				else if (!pagefileOnOsVolume || ToLower(drive[0]) == systemDrive)
				{
					const wchar_t* maxSizeStr = wcsrchr(line, ' ');
						
					if (!maxSizeStr || !StringBuffer<32>(maxSizeStr + 1).Parse(maxSizeMb))
					{
						logger.Warning(TC("Unrecognized page file information format (please report): %s"), line);
						continue;
					}

					if (maxSizeMb) // Custom set page file size
					{
						maxPageSize += maxSizeMb * 1024 * 1024;
						continue;
					}
				}
				else
				{
					logger.Warning(TC("Page file is set on drive %c: but registry key value PagefileOnOsVolume is set to 1. Fix registry"), drive[0]);
				}

				// Max possible system-managed page file
				maxSizeMb = Max(u64(memStatus.ullTotalPhys) * 3, 4ull * 1024 * 1024 * 1024);

				// Check if disk is limiting factor of system-managed page file
				// Page file can be max 1/8 of volume size and ofc not more than free space
				ULARGE_INTEGER totalNumberOfBytes;
				ULARGE_INTEGER totalNumberOfFreeBytes;
				if (!GetDiskFreeSpaceExW(drive.data, NULL, &totalNumberOfBytes, &totalNumberOfFreeBytes))
					return logger.Error(TC("GetDiskFreeSpaceExW failed to get information about %s (%s)"), drive.data, LastErrorToText().data);

				u64 maxDiskPageFileSize = Min(totalNumberOfBytes.QuadPart / 8, totalNumberOfFreeBytes.QuadPart);
				maxPageSize += Min(maxDiskPageFileSize, maxSizeMb);
			}
		}
		return maxPageSize;
	}
#endif

	void GetMachineId(StringBufferBase& out)
	{
		bool success = false;
		tchar str[256];

#if PLATFORM_WINDOWS
		DWORD strBytes = sizeof(str);
		success = RegGetValueW(HKEY_LOCAL_MACHINE, TC("SOFTWARE\\Microsoft\\Cryptography"), TC("MachineGuid"), RRF_RT_REG_SZ, NULL, str, &strBytes) == ERROR_SUCCESS;
		if (success)
			out.Append(str);
#elif PLATFORM_LINUX
		if (FILE* id = fopen("/etc/machine-id", "r"))
		{
			success = fgets(str, sizeof_array(str), id) != nullptr;
			fclose(id);
			if (success)
				out.Append(str);
		}
#else
		(void)str;
		success = GetComputerNameW(out); // TODO: Revisit below
		//if (FILE* id = popen("ioreg -rd1 -c IOPlatformExpertDevice | awk -F\\\" '/IOPlatformUUID/ { print $4 }'", "r"))
		//{
		//	success = fgets(str, sizeof(str), id) != nullptr;
		//	fclose(id);
		//	if (success)
		//		out.Append(str, strcspn(str, "\n"));
		//}
#endif
		if (!success)
			out.Append(TCV("NoMachineId"));
	}

	bool GetMemoryInfo(Logger& logger, u64& outAvailable, u64& outTotal, u64* outMaxPageFileSize)
	{
		if (outMaxPageFileSize)
			*outMaxPageFileSize = 0;

#if PLATFORM_WINDOWS
		MEMORYSTATUSEX memStatus = { sizeof(memStatus) };
		if (!GlobalMemoryStatusEx(&memStatus))
		{
			outAvailable = 0;
			outTotal = 0;
			return logger.Error(TC("Failed to get global memory status (%s)"), LastErrorToText().data);
		}

		// Page file can grow and we want to use the absolute max size to figure out when we need to wait to start new processes
		static u64 maxPageSize = GetMaxPageFileSize(logger, memStatus);

		if (outMaxPageFileSize)
			*outMaxPageFileSize = maxPageSize;

		u64 currentPageSize = memStatus.ullTotalPageFile - memStatus.ullTotalPhys;
		if (currentPageSize < maxPageSize)
		{
			outTotal = memStatus.ullTotalPhys + maxPageSize;
			outAvailable = memStatus.ullAvailPageFile + (maxPageSize - currentPageSize);
		}
		else
		{
			outTotal = memStatus.ullTotalPageFile;
			outAvailable = memStatus.ullAvailPageFile;
		}
#else
		u64 memKb;
		GetPhysicallyInstalledSystemMemory(memKb);
		outTotal = memKb*1024*1024;
		outAvailable = outTotal;
#endif
		return true;
	}

	void GetSystemInfo(Logger& logger, StringBufferBase& out)
	{
		u32 cpuCount = GetLogicalProcessorCount();
		u32 cpuGroupCount = GetProcessorGroupCount();

		StringBuffer<128> cpuStr(TC("CPU"));
		if (IsRunningArm())
			cpuStr.Append(TCV("[Arm]"));
		cpuStr.Append(':');
		if (cpuGroupCount != 1)
			cpuStr.AppendValue(cpuGroupCount).Append('x');
		cpuStr.AppendValue(cpuCount/cpuGroupCount);

		u64 totalMemoryInKilobytes = 0;

#if PLATFORM_WINDOWS
		GetPhysicallyInstalledSystemMemory(&totalMemoryInKilobytes);

		{
			u32 maxMHz = 0;
			DWORD valueSize = 4;
			const tchar* key = TC("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
			LSTATUS res = RegGetValueW(HKEY_LOCAL_MACHINE, key, TC("~MHz"), RRF_RT_REG_DWORD, NULL, &maxMHz, &valueSize);
			if (res != ERROR_SUCCESS)
			{
				// This will not always be the same and since we use the system info as part of key for client uniqueness it is annoying to get multiple sessions for same instance
				Vector<PROCESSOR_POWER_INFORMATION> procInfos;
				procInfos.resize(cpuCount);
				if (CallNtPowerInformation(ProcessorInformation, NULL, 0, procInfos.data(), cpuCount*sizeof(PROCESSOR_POWER_INFORMATION)) == STATUS_SUCCESS)
					maxMHz = procInfos[0].MaxMhz;
			}
			cpuStr.Appendf(TC(" @ %.1fGHz"), float(maxMHz) / 1000.0f);
		}

#else
		u64 throwAway;
		GetMemoryInfo(logger, throwAway, totalMemoryInKilobytes);
		totalMemoryInKilobytes /= 1024;
		double processorMhz = 0.0;
	#if PLATFORM_LINUX
		if (FILE *fp = fopen("/proc/cpuinfo", "r"))
		{
			char line[256];
			while (processorMhz == 0.0 && fgets(line, sizeof(line), fp))
				if (strncmp(line, "cpu MHz", 7) == 0)
					sscanf(line, "cpu MHz : %lf", &processorMhz);
			fclose(fp);
			cpuStr.Appendf(" @ %.1fGHz", processorMhz/1000.0);
		}
	#else
		char brand[128];
		size_t size = sizeof(brand);
		if (sysctlbyname("machdep.cpu.brand_string", brand, &size, NULL, 0) == 0)
			cpuStr.Clear().Append(brand).Appendf(" CPU:%u", cpuCount);
	#endif
#endif

		out.Appendf(TC("%s Mem:%ugb"), cpuStr.data, u32(totalMemoryInKilobytes/(1024*1024)));

		#if PLATFORM_WINDOWS
		if (!IsRunningWine())
		{
			DWORD value = 0;
			DWORD valueSize = 4;
			const tchar* fsKey = TC("SYSTEM\\CurrentControlSet\\Control\\FileSystem");
			LSTATUS res = RegGetValueW(HKEY_LOCAL_MACHINE, fsKey, TC("NtfsDisableLastAccessUpdate"), RRF_RT_REG_DWORD, NULL, &value, &valueSize);
			if (res != ERROR_SUCCESS)
			{
				logger.Detail(TC("Failed to retreive ntfs registry key (%i)"), res);
			}
			else
			{
				u32 lastAccessSettingsValue = value & 0xf;
				if (lastAccessSettingsValue == 0 || lastAccessSettingsValue == 2)
					out.Append(TCV(" NtfsLastAccessEnabled"));
			}
			value = 0;
			res = RegGetValueW(HKEY_LOCAL_MACHINE, fsKey, TC("NtfsDisable8dot3NameCreation"), RRF_RT_REG_DWORD, NULL, &value, &valueSize);
			if (res == ERROR_SUCCESS)
				if (value == 0)
					out.Append(TCV(" NtfsShortNamesEnabled"));
		}
		else
		{
			//StringBuffer<> testDir;
			//testDir.Append(m_rootDir).Append(TCV("UbaTestShortNames"));
			//::RemoveDirectory(testDir.data);
			//wchar_t shortName[1024];
			//if (::CreateDirectoryW(testDir.data, NULL))
			//	if (GetShortPathName(testDir.data, shortName, 1024) != 0 && !Contains(shortName, TC("UbaTestShortNames")))
			//		out.Append(TCV(" NtfsShortNamesEnabled"));
		}
		#endif
	}

	bool GetCpuTime(u64& outTotalTime, u64& outIdleTime)
	{
		outTotalTime = 0;
		outIdleTime = 0;
#if PLATFORM_WINDOWS

		GROUP_AFFINITY originalAffinity {};
		GROUP_AFFINITY newAffinity{};
		static u16 groupCount = GetActiveProcessorGroupCount();
		if (groupCount <= 1)
		{
			u64 idleTime, kernelTime, userTime;
			if (!GetSystemTimes((FILETIME*)&idleTime, (FILETIME*)&kernelTime, (FILETIME*)&userTime))
				return false;
			outIdleTime += idleTime;
			outTotalTime += kernelTime + userTime;
		}
		else
		{
			for (u16 group=0; group!=groupCount; ++group)
			{
				newAffinity.Mask = ~0ull;
				newAffinity.Group = group;
				if (!SetThreadGroupAffinity(GetCurrentThread(), &newAffinity, group == 0 ? &originalAffinity : NULL))
					return false;
				u64 idleTime, kernelTime, userTime;
				if (!GetSystemTimes((FILETIME*)&idleTime, (FILETIME*)&kernelTime, (FILETIME*)&userTime))
					return false;
				outIdleTime += idleTime;
				outTotalTime += kernelTime + userTime;
			}
			if (!SetThreadGroupAffinity(GetCurrentThread(), &originalAffinity, nullptr))
				return false;
		}

#elif PLATFORM_LINUX
		int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
		if (fd == -1)
			return false;
		char buffer[512];
		int size = read(fd, buffer, sizeof_array(buffer) - 1);
		close(fd);
		if (size == -1)
			return false;
		buffer[size] = 0;
		char* endl = strchr(buffer, '\n');
		if (!endl)
			return false;
		u64 values[16] = { 0 };
		u32 valueCount = 0;
		*endl = 0;
		char* parsePos = buffer;

		// cpu
		parsePos = strchr(parsePos, ' ');
		if (!parsePos)
			return false;

		while (true)
		{
			// remove spaces
			while (*parsePos && (*parsePos < '0' || *parsePos > '9'))
				++parsePos;
			if (!*parsePos)
				break;
			char* numberStart = parsePos;
			while (*parsePos && *parsePos >= '0' && *parsePos <= '9')
				++parsePos;
			bool isLast = *parsePos == 0;
			*parsePos = 0;
			++parsePos;
			values[valueCount++] = strtoull(numberStart, nullptr, 10);
			if (isLast)
				break;
		}

		// user: normal processes executing in user mode
		// nice: niced processes executing in user mode
		// system: processes executing in kernel mode
		// idle: twiddling thumbs
		// iowait: waiting for I/O to complete
		// irq: servicing interrupts
		// softirq: servicing softirqs
		// steal
		if (valueCount <= 6)
			return false;
		u64 work = values[0] + values[1] + values[2];
		outIdleTime = values[3] + values[4] + values[5] + values[6] + values[7];
		outTotalTime = work + outIdleTime;

#else // PLATFORM_MAC
		mach_msg_type_number_t  CpuMsgCount = 0;
		processor_flavor_t CpuInfoType = PROCESSOR_CPU_LOAD_INFO;;
		natural_t CpuCount = 0;
		processor_cpu_load_info_t CpuData;
		host_t host = mach_host_self();
		u64 work = 0;
		int res = host_processor_info(host, CpuInfoType, &CpuCount, (processor_info_array_t *)&CpuData, &CpuMsgCount);
		if(res != KERN_SUCCESS)
			return false;//m_logger.Error(TC("Kernel error: %s"), mach_error_string(res));
		for(int i = 0; i < (int)CpuCount; i++)
		{
			work += CpuData[i].cpu_ticks[CPU_STATE_SYSTEM];
			work += CpuData[i].cpu_ticks[CPU_STATE_USER];
			work += CpuData[i].cpu_ticks[CPU_STATE_NICE];
			outIdleTime += CpuData[i].cpu_ticks[CPU_STATE_IDLE];
		}
		outTotalTime = work + outIdleTime;
#endif
		return true;
	}
}
