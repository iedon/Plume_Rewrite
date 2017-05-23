#include "stdafx.h"
#include "PLAPI.h"
#include "Plume_Rewrite.h"
#include "windows.h"
#include "shlwapi.h"
#pragma comment(lib, "shlwapi.lib")

#define PLUGIN_NAME "URL Rewrite"
#define PLUGIN_VER "1.0.2"
#define PLUGIN_AUTHOR "iEdon"
#define PLUGIN_DESC "提供URL重写与伪静态功能。"

enum rule_type { REWRITE_COND, REWRITE_RULE, REWRITE_BASE, REWRITE_ERROR_TYPE };

typedef struct rewrite_rule {
	std::string line;
	rule_type type;
	
	regex_t pattern;		// eg. ^(.*)$
	std::string flags;		// eg. [NC]
	
	std::string format;		// only for REWRITE_RULE eg. /$1/index.php
	std::string cond;		// only for REWRITE_COND eg. %{HTTP_HOST}
	bool isCondSatisfied;
	rewrite_rule *next;
}rewrite_rule;

int Rewrite(plume_server *s, int dwEventType);
int replace_all(std::string& str, const std::string& pattern, const std::string& newpat);
rewrite_rule *head = NULL;

PLAPI void WINAPI Plume_GetPluginDesc(char **lpszName, char **lpszVersion, char **lpszAuthor, char **lpszDescription)
{
	*lpszName = PLUGIN_NAME;
	*lpszVersion = PLUGIN_VER;
	*lpszAuthor = PLUGIN_AUTHOR;
	*lpszDescription = PLUGIN_DESC;
}

/*

// release rule list
rewrite_rule *p = head, *next;
do {
	next = p->next;
	free(p);
	p = next;
} while (p != NULL);

*/

PLAPI bool WINAPI Plume_InitPlugin(int dwServerVersion, PLAPI_ServerFunc)
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

	// get rewrite file path
	char config[MAX_PATH_LEN];
	GetCurrentDirectoryA(MAX_PATH_LEN, config);
	strcat(config, "\\Plugins\\rewrite.txt");

	// open file
	FILE* config_file;
	if (fopen_s(&config_file, config, "r") != 0)
	{
		std::string clog = "\tUnable to open rewrite configuration.\n";
		ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, (void *)clog.c_str(), clog.size());
		printf(clog.c_str());
		return false;
	}

	// read rules
	head = (rewrite_rule*)malloc(sizeof rewrite_rule);
	rewrite_rule* p = head;
	for (;;) {
		int c;
		for (c = fgetc(config_file); c != -1 && c != '\n' && c!= '\r' && c!='#'; c = fgetc(config_file))
			p->line += c;
		std::cout << p->line.c_str() << std::endl;
		if (c == -1)
		{
			// no more rules
			p->next = NULL;
			break;
		}
		if (!p->line.empty())
		{
			// contains content, turn to next rule.
			p->next = (rewrite_rule*)malloc(sizeof rewrite_rule);
			p = p->next;
		}
	}
	fclose(config_file);

	// walk rules
	p = head;
	do {
		p->isCondSatisfied = true;
		if (p->line.compare(0, 16/*strlen("RewriteEngine On")*/, "RewriteEngine On") == 0)
		{
			// delete the useless rule
			rewrite_rule *thiz = p;
			head = p = p->next;
			free(thiz);
			continue;
		} else if (p->line.compare(0, 12/*strlen("RewriteBase ")*/, "RewriteBase ") == 0) {
			p->type = REWRITE_BASE;
			p->line = p->line.substr(12);
		} else if (p->line.compare(0, 12/*strlen("RewriteCond ")*/, "RewriteCond ") == 0) {
			p->type = REWRITE_COND;
			p->line = p->line.substr(12);

			// parse RewriteCond
			// eg. %{HTTP_HOST} ^www\.(.*) [NC]
			std::string rewrite_pattern;
			size_t cond_end = p->line.find(' ');
			if (cond_end == std::string::npos)
				continue;	// invalidate RewriteCond
			else {
				size_t pattern_end = p->line.find(' ', cond_end + 1);
				if (pattern_end == std::string::npos) {
					p->cond = p->line.substr(0, cond_end);				// eg. %{HTTP_HOST}
					rewrite_pattern = p->line.substr(cond_end + 1);		// eg. ^www\.(.*)
				}
				else {
					p->cond = p->line.substr(0, cond_end);
					rewrite_pattern = p->line.substr(cond_end + 1, pattern_end - cond_end - 1);
					p->flags = p->line.substr(pattern_end + 1);			// eg. [NC]
				}
			}

			// compile regex
			int reg_ret;
			if (p->flags.find("NC", 0) != std::string::npos)
			{
				reg_ret = regcomp(&p->pattern, rewrite_pattern.c_str(), REG_EXTENDED | REG_ICASE);
			}
			else
			{
				reg_ret = regcomp(&p->pattern, rewrite_pattern.c_str(), REG_EXTENDED);
			}
			if (reg_ret != 0)
			{
				// failed to compile regex
				char strbuff[512];
				regerror(reg_ret, &p->pattern, strbuff, 512);
				char errbuff[1024] = { 0 };
				sprintf(errbuff, "rewrite error: %d(%s) pattern: %s", reg_ret, strbuff, rewrite_pattern.c_str());
				ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, errbuff, strlen(errbuff));
				printf(errbuff);
			}
		} else if (p->line.compare(0, 12/*strlen("RewriteRule ")*/, "RewriteRule ") == 0) {
			p->type = REWRITE_RULE;
			p->line = p->line.substr(12);

			// parse RewriteRule
			// eg. ^(.*)$ http://%1/$1 [R=301,NC,L]
			std::string rewrite_pattern;
			size_t pattern_end = p->line.find(' ');
			if (pattern_end == std::string::npos)
				continue;	// invalidate RewriteRule
			else {
				size_t format_end = p->line.find(' ', pattern_end + 1);
				if (format_end == std::string::npos) {
					rewrite_pattern = p->line.substr(0, pattern_end);	// eg. ^(.*)$
					p->format = p->line.substr(pattern_end + 1);		// eg. http://%1/$1
				}
				else {
					rewrite_pattern = p->line.substr(0, pattern_end);
					p->format = p->line.substr(pattern_end + 1, format_end - pattern_end - 1);
					p->flags = p->line.substr(format_end + 1);			// eg. [R=301,NC,L]
				}
			}

			// compile regex
			int reg_ret;
			if (p->flags.find("NC", 0) != std::string::npos)
			{
				reg_ret = regcomp(&p->pattern, rewrite_pattern.c_str(), REG_EXTENDED | REG_ICASE);
			}
			else
			{
				reg_ret = regcomp(&p->pattern, rewrite_pattern.c_str(), REG_EXTENDED);
			}
			if (reg_ret != 0)
			{
				// failed to compile regex
				char strbuff[512];
				regerror(reg_ret, &p->pattern, strbuff, 512);
				char errbuff[1024] = { 0 };
				sprintf(errbuff, "rewrite error: %d(%s) pattern: %s", reg_ret, strbuff, rewrite_pattern.c_str());
				ServerFunc(NULL, NULL, PLUME_ACTION_LOG, NULL, errbuff, strlen(errbuff));
				printf(errbuff);
			}
		} else {
			// could be "Options +FollowSymlinks"...
			p->type = REWRITE_ERROR_TYPE;
		}
	} while ((p = p->next) != NULL);

	// compeleted
	ServerFunc(NULL, NULL, PLUME_ACTION_REGISTER_EVENT, PLUME_EVENT_BEGIN_REQUEST, Rewrite, NULL);
	return true;
}

PLAPI int WINAPI Plume_PluginProc(plume_server * s)
{
	// Prepare context
	char *lpvBuffer = (char *)malloc(4096);
	memset(lpvBuffer, 0, 4096);
	s->GetEnvVar(s->dwServerID, s->dwConnID, "SERVER_SOFTWARE", lpvBuffer);
	char lpszContext[1024];
	sprintf(lpszContext, "<h1>Plume URL Rewrite Plugin</h1><br />You are running : %s", lpvBuffer);

	// Prepare header
	char lpszHeader[128];
	sprintf(lpszHeader, "Content-Type: text/html; charset=gb2312\r\nContent-Length: %d", strlen(lpszContext));

	s->ServerFunc(s->dwServerID, s->dwConnID, PLUME_ACTION_SEND_HEADER, NULL, lpszHeader, strlen(lpszHeader)); // Send Header

	s->Write(s->dwServerID, s->dwConnID, lpszContext, strlen(lpszContext)); // Send Context

	s->ServerFunc(s->dwServerID, s->dwConnID, PLUME_ACTION_DONE_REQUEST, NULL, NULL, NULL); // Done Request.

	free(lpvBuffer);
	return PLUME_PROC_RESULT_SUCCESS;
}

int Rewrite(plume_server *s, int dwEventType)
{
	if (dwEventType == PLUME_EVENT_BEGIN_REQUEST) // Begin Request
	{
		if (head == NULL) return PLUME_EVENT_RESULT_CONTINUE;

		char **pConnDIR,		// eg. D:\wwwroot
			**pPathTranslated,	// eg. D:\wwwroot\a.jpg
			**pPath;			// eg. /a.jpg
		Plume_GetPathStruct(s, &pConnDIR, &pPathTranslated, &pPath);
		std::cout << "ALIVE.";
		std::string tmpPath = *pPath;
		std::cout << "STILL ALIVE.";
		//const rewrite_rule *p = head;
		rewrite_rule *p = head;
		
		do {
			switch (p->type) {
			case REWRITE_BASE:
			{
				// eg1. "" + "/index.php" => "/index.php"
				// eg2. "RewriteBase /" + "index.php" => "/index.php"
				// eg3. "RewriteBase /foo" + "index.php" => "/foo/index.php"
				std::cout << "REWRITE_BASE: " << p->line.c_str() << std::endl;
				std::string rewrite_base = p->line;
				break;
			}
			case REWRITE_COND:
			{
				// eg. %{HTTP_HOST} ^www\.(.*) [NC]
				std::cout << "REWRITE_COND: " << p->cond.c_str() << std::endl;
				std::string cond = p->cond;
				replace_env_strings(cond, *pConnDIR, *pPathTranslated, *pPath, s);

				// match pattern
				regmatch_t pm[999];
				if (regexec(&p->pattern, *pPath, 999, pm, 0) == REG_NOMATCH)
				{
					p->isCondSatisfied = false;
					continue;
				}
				
				// p->flags
				break;
			}
			case REWRITE_RULE:
			{
				// match pattern
				std::cout << "REWRITE_RULE: " << p->format.c_str() << std::endl;
				regmatch_t pm[999];

				bool isLast = (p->flags.find("[L", 0) != std::string::npos
					|| p->flags.find("L]", 0) != std::string::npos
					|| p->flags.find(",L", 0) != std::string::npos
					|| p->flags.find(", L", 0) != std::string::npos
					|| p->flags.find("L,", 0) != std::string::npos
					|| p->flags.find("L ,", 0) != std::string::npos);

				if (!p->isCondSatisfied && isLast)
					return 1;
				if (!p->isCondSatisfied && !isLast)
					continue;
				bool isNoMatch = (regexec(&p->pattern, *pPath, 999, pm, 0) == REG_NOMATCH);

				if (isNoMatch && !isLast)
					continue;

				// regex replace
				char* result;
				NEW(result, MAX_PATH_LEN + 4);
				reg_replace(result, tmpPath.c_str(), p->format.c_str(), pm);
				tmpPath = result;
				// replace '/' => '\'
				int i = 0;
				for (; result[i] != '\0'; ++i) {
					if (result[i] == '/')
						result[i] = '\\';
					else if (result[i] == '?')
						break;	// remove query string
				}
				
				// p->flags

				// set pPathTranslated
				sprintf_s(*pPathTranslated, MAX_PATH, "%s%s", *pConnDIR,
					std::string(result).substr(0, i).c_str());
				DEL(result);
				std::cout << "!!!!!!LIVE!!!.";
				if (isLast)
					return PLUME_EVENT_RESULT_CONTINUE;
				//printf("orig uri: %s, local: %s\n", *pPath, *pPathTranslated);
				break;
			}
			case REWRITE_ERROR_TYPE:
				std::cout << "REWRITE_ERR: " << std::endl;
				break;
			}
		} while ((p = p->next) != NULL);
	}
	return PLUME_EVENT_RESULT_CONTINUE;
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

void reg_replace(char *result, const char* src, const char* format, regmatch_t pm[])
{
	int i = 0;
	for (const char* v = format; *v; v++)
	{
		// there is a known bug: can NOT identify '\$'.
		if (*v == '$' && *(v + 1) >= '0'&& *(v + 1) <= '9')
		{
			// ( ) substring
			char match_id[4] = { 0 };	// max 3 digits id -> max 999 matches
			int id_length = 0;
			for (char c = *++v; id_length < 4 && c >= '0'&& c <= '9'; ++v)
			{
				match_id[id_length++] = *v;
			}
			int k = atoi(match_id);
			if (pm[k].rm_so == -1)
				continue;	// eg. $999 dosen't exist.
			for (int j = pm[k].rm_so; i < MAX_PATH_LEN && j < pm[k].rm_eo; j++)
			{
				result[i++] = src[j];
			}
		}
		else if (i < MAX_PATH_LEN)
		{
			result[i++] = *v;
		}
	}
	result[i] = '\0';
}

void replace_env_strings(std::string& cond, const char* pConnDIR, const char* pPathTranslated, const char* pPath, plume_server *s)
{
	size_t pos = cond.find("%{");
	std::string env_var;
	if(pos != std::string::npos)
	{
		cond += 2;
		size_t end_pos = cond.find("}");
		if(end_pos == std::string::npos)
		{
			return;
		}
		env_var = cond.substr(pos + 2, end_pos - pos - 2);
	}
	else
	{
		return;
	}

	char lpvBuffer[4096] = { 0 };
	s->GetEnvVar(s->dwServerID, s->dwConnID, env_var.c_str(), lpvBuffer);
	cond.replace(pos, env_var.length() + 3, lpvBuffer);
}