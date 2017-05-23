#pragma once
#ifndef WINAPI
#define WINAPI __stdcall
#endif
#define PLAPI extern "C" __declspec(dllexport)
#define PLAPI_ServerFunc bool (WINAPI *ServerFunc)(int dwServerID, int dwConnID, int dwAction, int dwFlag, void *lpvBuffer, int ilength)
#define PLAPI_GetEnvVar bool(WINAPI *GetEnvVar)(int dwServerID, int dwConnID, const char *lpszVarName, void *lpvBuffer)
#define PLAPI_Read bool(WINAPI *Read)(int dwServerID, int dwConnID, void *lpvBuffer, int dwBytesToRead, int *dwBytesRead)
#define PLAPI_Write bool(WINAPI *Write)(int dwServerID, int dwConnID, const void *lpvBuffer, int ilength)
#define NEW(p, size) { p = (char *)malloc(size); memset( p, 0, size ); }
#define DEL(p) { free((void*)p); p = NULL; }
#define MAX_PATH_LEN 1024

#define PLUME_EVENT_BEGIN_CONN 1
#define PLUME_EVENT_DONE_CONN 2
#define PLUME_EVENT_BEGIN_REQUEST 3
#define PLUME_EVENT_DONE_REQUEST 4
#define PLUME_EVENT_RESULT_STOP 0
#define PLUME_EVENT_RESULT_CONTINUE 1
#define PLUME_EVENT_RESULT_DISCONNECT 2

#define PLUME_ACTION_REGISTER_EVENT 0
#define PLUME_ACTION_LOG 1
#define PLUME_ACTION_SEND_CUSTOM_ERROR 2
#define PLUME_ACTION_READ_CONFIG 3
#define PLUME_ACTION_SEND_HEADER 4
#define PLUME_ACTION_DONE_REQUEST 5

#define PLUME_PROC_RESULT_SUCCESS 1
#define PLUME_PROC_RESULT_FAILURE 0

typedef struct plume_server
{
	int dwServerID;
	int dwConnID;
	PLAPI_GetEnvVar;
	PLAPI_Read;
	PLAPI_Write;
	PLAPI_ServerFunc;
};
typedef struct plume_path_struct
{
	char* pConnDIR;
	char* pPathTranslated;
	char* pPath;
};
PLAPI bool WINAPI Plume_InitPlugin(int dwServerVersion, PLAPI_ServerFunc);
PLAPI int WINAPI Plume_PluginProc(plume_server * plume_server);
PLAPI void WINAPI Plume_GetPluginDesc(char **lpszName, char **lpszVersion, char **lpszAuthor, char **lpszDescription);

// Usage :
// char **pConnDIR, **pPathTranslated, **pPath, **pPathInfo;
// Plume_GetPathStruct(s, &pConnDIR, &pPathTranslated, &pPath, &pPathInfo);
// strcpy(*..., "Dest String");
bool Plume_GetPathStruct(plume_server *s, char ***pConnDIR, char ***pPathTranslated, char ***pPath)
{
	int ptr;
	if (!s->GetEnvVar(s->dwServerID, s->dwConnID, "Plume_PathStruct", &ptr))
		return false;

	*pConnDIR = (char **)ptr;
	*pPathTranslated = (char **)(ptr + 4);
	*pPath = (char **)(ptr + 8);
	return true;
}