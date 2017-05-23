#include "stdafx.h"
#include "Plume_Rewrite.h"
#include "regex.h"
#include "Markup.h"
#include <iostream>
#include "windows.h"
#include "shlwapi.h"
#pragma comment(lib, "shlwapi.lib")

#define PLUGIN_NAME "URL Rewrite"
#define PLUGIN_VER "1.0.1"
#define PLUGIN_AUTHOR "iEdon"
#define PLUGIN_DESC "提供URL重写和伪静态的功能。"

typedef struct rewrite_rule {
	char		host[128];
	char		base[64];
	char		base_len;
	char		result[128];
	char		cond[512];
	regex_t		compiled_pattern;
	rewrite_rule *next;
}rewrite_rule;

int Rewrite(plume_server *s, int dwEventType);
int replace_all(std::string& str, const std::string& pattern, const std::string& newpat);
char *Config = NULL;
CMarkup conf;
rewrite_rule *head;

PLAPI_INTERFACE void PLAPI_CALLBACK Plume_GetPluginDesc(char **lpszName, char **lpszVersion, char **lpszAuthor, char **lpszDescription)
{
	*lpszName = PLUGIN_NAME;
	*lpszVersion = PLUGIN_VER;
	*lpszAuthor = PLUGIN_AUTHOR;
	*lpszDescription = PLUGIN_DESC;
}

PLAPI_INTERFACE bool PLAPI_CALLBACK Plume_FreePlugin()
{
	conf.~CMarkup();
	free(Config);
	return true;
}

PLAPI_INTERFACE bool PLAPI_CALLBACK Plume_InitPlugin(int dwServerVersion, PLAPI_ServerFunc)
{
	char *log = "\tPlume URL Rewrite Plugin v1.0.1\n";
	ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, log, strlen(log));
	printf(log);

	if (MAKEWORD(4, 0) > dwServerVersion)
	{
		log = "Not supported version yet.\n";
		ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, log, strlen(log));
		printf(log);
		return false;
	}
	NEW(Config, MAX_PATH_LEN);
	GetCurrentDirectoryA(MAX_PATH_LEN, Config);
	strcat(Config, "\\Plugins\\Plume_Rewrite.xml");
	bool ret = conf.Load(Config);
	if (!ret)
	{
		std::string clog = "Unable to load rewrite configuration: ";
		clog.append(conf.GetError().c_str());
		clog.append("\n");
		ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, (void *)clog.c_str(), strlen(clog.c_str()));
		printf(clog.c_str());
		DEL(Config);
		return false;
	}
	char tmp[128] = {0};
	char host[128] = {0};
	rewrite_rule *last = NULL;
	conf.ResetMainPos();
	if (conf.FindElem("Plume_Rewrite"))
	{
		if (conf.FindChildElem("http_host"))
		{
			do {
				memset(host, 0, 128);
				strncpy(host, conf.GetChildAttrib("name").c_str(), 127);
				conf.IntoElem();
				if (conf.FindChildElem("rewrite")) {
					do {
						char base[64];
						strncpy(base, conf.GetChildAttrib("base").c_str(), 63);
						int base_len = strlen(base);
						//load rules
						conf.IntoElem();
						if (conf.FindChildElem("rule")) {
							do {
								conf.IntoElem();
								int ret;
								rewrite_rule * rule;
								rule = (rewrite_rule *)malloc(sizeof(rewrite_rule));
								memset(rule, 0, sizeof(rewrite_rule));
								if (last != NULL)
								{
									last->next = rule;
								}
								else
								{
									head = rule;
								}
								last = rule;
								strcpy(rule->host, host);
								strcpy(rule->base, base);
								rule->base_len = base_len;
								strncpy(rule->cond, conf.GetAttrib("cond").c_str(), 261);
								conf.FindChildElem("pattern");
								std::string pattern = conf.GetChildData();
								conf.FindChildElem("result");
								strncpy(rule->result, conf.GetChildData().c_str(), 127);
								//printf("base: %s, pattern: %s, result: %s, at host: %s\n", rule->base, pattern.c_str(), rule->result, rule->host);
								if ((ret = regcomp(&rule->compiled_pattern, pattern.c_str(), REG_EXTENDED)) != 0)
								{
									memset(tmp, 0, 128);
									regerror(ret, &rule->compiled_pattern, tmp, 64);
									char err[1024] = { 0 };
									sprintf(err, "rewrite error: %d(%s)  pattern: %s host: %s\n", ret, tmp, pattern.c_str(), host);
									ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, err, strlen(err));
									printf(err);
								}
								conf.OutOfElem();
							} while (conf.FindChildElem("rule"));
						}
						conf.OutOfElem();
					} while (conf.FindChildElem("rewrite"));
				}
				conf.OutOfElem();
			} while (conf.FindChildElem("http_host"));
		}
	}
	ServerFunc(NULL, NULL, NULL, PLUME_EVENT_BEGIN_REQUEST, Rewrite, NULL); // 注册插件事件
	return true;
}

PLAPI_INTERFACE int PLAPI_CALLBACK Plume_PluginProc(plume_server * s)
{
	// Prepare context
	char *lpvBuffer = (char *)malloc(4096);
	ZeroMemory(lpvBuffer, 4096);
	s->GetEnvVar(s->dwServerID, s->dwConnID, "SERVER_SOFTWARE", lpvBuffer);
	char lpszContext[1024];
	sprintf(lpszContext, "<h1>Plume URL Rewrite Plugin</h1><br />You are running : %s", lpvBuffer);

	// Prepare header
	char lpszHeader[128];
	sprintf(lpszHeader, "Content-Type: text/html; charset=gb2312\r\nContent-Length: %d", strlen(lpszContext));

	s->ServerFunc(s->dwServerID, s->dwConnID, 4, NULL, lpszHeader, strlen(lpszHeader)); // Send Header

	s->Write(s->dwServerID, s->dwConnID, lpszContext, strlen(lpszContext)); // Send Context

	s->ServerFunc(s->dwServerID, s->dwConnID, 5, NULL, NULL, NULL); // Done Request.

	free(lpvBuffer);
	return 1;
}

int Rewrite(plume_server *s, int dwEventType)
{
	if (dwEventType == PLUME_EVENT_BEGIN_REQUEST) // Begin Request
	{
		char **pConnDIR, **pPathTranslated, **pPath, **pPathInfo;
		Plume_GetPathStruct(s, &pConnDIR, &pPathTranslated, &pPath, &pPathInfo);
		char *HTTP_HOST = (char *)malloc(4096);
		memset(HTTP_HOST, 0, 4096);
		s->GetEnvVar(s->dwServerID, s->dwConnID, "HTTP_HOST", HTTP_HOST);
		rewrite_rule *p = head;
		bool found = false;
		//printf("p->host: %s, HTTP_HOST: %s\n", p->host, HTTP_HOST);
		if (p != NULL)
		{
			do
			{
				//printf("entering... %s\n", HTTP_HOST);
				if (strcmp(p->host, HTTP_HOST) == 0)
					found = true;
				else
					found = false;
				if (found)
				{
					if (strlen(p->cond) > 0)
					{
						bool exist = false;
						char comp[2] = { 0 };
						comp[0] = p->cond[0];
						if (strcmp(comp, "!") == 0)
						{
							exist = true;
						}
						std::string cond_path = p->cond;
						if(exist == true)
							cond_path = cond_path.substr(1, strlen(cond_path.c_str()));
						replace_all(cond_path,"{REQUEST_FILENAME}", *pPathTranslated);
						replace_all(cond_path, "/", "\\");
						//printf("cond_path: %s\n", cond_path);
						if (PathFileExistsA(cond_path.c_str()))
						{
							if (exist)
							{
								p = p->next;
								continue;
							}
						}
						else
						{
							if (!exist)
							{
								p = p->next;
								continue;
							}
						}
					}
					const int nmatch = 9; //why is nine? enough?
					char* newname;
					int i;
					char* v;
					regmatch_t pm[nmatch];
					if (*pPath[0] != '/')
					{ //bad luck!!
						NEW(newname, MAX_PATH_LEN + 4);
						if (strcmp(*pConnDIR, "/") == 0)
							sprintf(newname, "/%s", *pPath);
						else
							sprintf(newname, "%s/%s", *pConnDIR, *pPath);
						strcpy(*pPath, newname);
						DEL(newname);
					}
					if (strncmp(*pPath, p->base, p->base_len))
					{
						p = p->next;
						continue;
					} //search next rule
					if (regexec(&p->compiled_pattern, *pPath, nmatch, pm, 0) == REG_NOMATCH)
					{
						p = p->next;
						continue;
					} //search next rule
					NEW(newname, MAX_PATH_LEN + 4);
					//printf("result: %s\n", p->result);
					for (v = p->result, i = 0; *v; v++)
					{
						if (*v == '$' && *(v + 1) >= '0'&& *(v + 1) <= '9')
						{
							int k = *(++v) - '0', j = 0;
							if (pm[k].rm_so == -1)
							{
								p = p->next;
								continue;
							}
							for (j = pm[k].rm_so; i < MAX_PATH_LEN && j < pm[k].rm_eo; j++)
							{
								newname[i++] = (*(pPath)+j)[0];
							}
						}
						else if (i < MAX_PATH_LEN)
						{
							newname[i++] = *v;
						}
					}
					newname[i] = '\0';
					//printf("newname: %s\n", newname);
					std::string pt = newname;
					DEL(newname);
					replace_all(pt, "/", "\\");
					int ipos = pt.find("?", 0);
					if (ipos != std::string::npos)
					{
						pt = pt.substr(0, ipos);
					}
					std::string locate = *pConnDIR;
					locate.append(pt);
					strcpy(*pPathTranslated, locate.c_str());
					strcpy(*pPathInfo, "\0");
					//printf("orig uri: %s, local: %s\n", *pPath, *pPathTranslated);
					break;
				}
				p = p->next;
			} while (p != NULL);
		}
		free(HTTP_HOST);
		return 1; //finished.
	}
	return 1;
}

int replace_all(std::string& str, const std::string& pattern, const std::string& newpat)
{
	int count = 0;
	const size_t nsize = newpat.size();
	const size_t psize = pattern.size();

	for (size_t pos = str.find(pattern, 0);
		pos != std::string::npos;
		pos = str.find(pattern, pos + nsize))
	{
		str.replace(pos, psize, newpat);
		count++;
	}

	return count;
}