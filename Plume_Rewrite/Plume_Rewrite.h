#pragma once
#include "regex.h"
#include <iostream>
void replace_env_strings(std::string& cond, const char* pConnDIR, const char* pPathTranslated, const char* pPath, plume_server *s);
void reg_replace(char *result, const char* src, const char* format, regmatch_t pm[]);
int replace_all(std::string& str, const std::string& pattern, const std::string& newpat);
int Rewrite(plume_server *s, int dwEventType);