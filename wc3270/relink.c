/*
 * Copyright (c) 2006-2009, Paul Mattes.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Paul Mattes nor his contributors may be used
 *       to endorse or promote products derived from this software without
 *       specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	relink.c
 *		A Windows console-based 3270 Terminal Emulator
 *		Utility functions to read a session file and create a
 *		compatible shortcut.
 */

#include "globals.h"

#include <signal.h>
#include "appres.h"
#include "3270ds.h"
#include "resources.h"
#include "ctlr.h"

#include "actionsc.h"
#include "ctlrc.h"
#include "hostc.h"
#include "keymapc.h"
#include "kybdc.h"
#include "macrosc.h"
#include "screenc.h"
#include "tablesc.h"
#include "trace_dsc.h"
#include "utilc.h"
#include "xioc.h"

#include <windows.h>
#include <direct.h>
#include <wincon.h>
#include <shlobj.h>
#include "shlobj_missing.h"

#include "winversc.h"
#include "shortcutc.h"
#include "windirsc.h"

#if defined(_MSC_VER) /*[*/
#include "Msc/deprecated.h"
#endif /*]*/

#include "relinkc.h"

charsets_t charsets[] = {
	{ "belgian",		"500",	0, L"1252"	},
	{ "belgian-euro",	"1148",	0, L"1252"	},
	{ "bracket",		"37*",	0, L"1252"	},
	{ "brazilian",		"275",	0, L"1252"	},
	{ "cp1047",		"1047",	0, L"1252"	},
	{ "cp870",		"870",	0, L"1250"	},
	{ "chinese-gb18030",	"1388",	1, L"936"	},
	{ "finnish",		"278",	0, L"1252"	},
	{ "finnish-euro",	"1143",	0, L"1252"	},
	{ "french",		"297",	0, L"1252"	},
	{ "french-euro",	"1147",	0, L"1252"	},
	{ "german",		"273",	0, L"1252"	},
	{ "german-euro",	"1141",	0, L"1252"	},
	{ "greek",		"875",	0, L"1253"	},
	{ "hebrew",		"424",	0, L"1255"	},
	{ "icelandic",		"871",	0, L"1252"	},
	{ "icelandic-euro",	"1149",	0, L"1252"	},
	{ "italian",		"280",	0, L"1252"	},
	{ "italian-euro",	"1144",	0, L"1252"	},
	{ "japanese-kana",	"930",  1, L"932"	},
	{ "japanese-latin",	"939",  1, L"932"	},
	{ "norwegian",		"277",	0, L"1252"	},
	{ "norwegian-euro",	"1142",	0, L"1252"	},
	{ "russian",		"880",	0, L"1251"	},
	{ "simplified-chinese",	"935",  1, L"936"	},
	{ "spanish",		"284",	0, L"1252"	},
	{ "spanish-euro",	"1145",	0, L"1252"	},
	{ "thai",		"838",	0, L"1252"	},
	{ "traditional-chinese","937",	1, L"950"	},
	{ "turkish",		"1026",	0, L"1254"	},
	{ "uk",			"285",	0, L"1252"	},
	{ "uk-euro",		"1146",	0, L"1252"	},
	{ "us-euro",		"1140",	0, L"1252"	},
	{ "us-intl",		"37",	0, L"1252"	},
	{ NULL,			NULL,	0, NULL	}
};

size_t num_charsets = (sizeof(charsets) / sizeof(charsets[0])) - 1;

             /*  model  2   3   4   5 */
static int wrows[6] = { 0, 0, 25, 33, 44, 28  };
static int wcols[6] = { 0, 0, 80, 80, 80, 132 };

static char *user_settings = NULL;

static wchar_t *
reg_font_from_cset(char *cset, int *codepage)
{
    	int i, j;
	wchar_t *cpname = NULL;
	wchar_t data[1024];
	DWORD dlen;
	HKEY key;
	static wchar_t font[1024];
	DWORD type;

	*codepage = 0;

    	/* Search the table for a match. */
	for (i = 0; charsets[i].name != NULL; i++) {
	    	if (!strcmp(cset, charsets[i].name)) {
		    	cpname = charsets[i].codepage;
		    	break;
		}
	}

	/* If no match, use Lucida Console. */
	if (cpname == NULL)
	    	return L"Lucida Console";

	/*
	 * Look in the registry for the console font associated with the
	 * Windows code page.
	 */
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		    "Software\\Microsoft\\Windows NT\\CurrentVersion\\"
		    "Console\\TrueTypeFont",
		    0,
		    KEY_READ,
		    &key) != ERROR_SUCCESS) {
	    	printf("RegOpenKey failed -- cannot find font\n");
		return L"Lucida Console";
	}
	dlen = sizeof(data);
    	if (RegQueryValueExW(key,
		    cpname,
		    NULL,
		    &type,
		    (LPVOID)data,
		    &dlen) != ERROR_SUCCESS) {
	    	/* No codepage-specific match, try the default. */
	    	dlen = sizeof(data);
	    	if (RegQueryValueExW(key, L"0", NULL, &type, (LPVOID)data,
			    &dlen) != ERROR_SUCCESS) {
			RegCloseKey(key);
			printf("RegQueryValueEx failed -- cannot find font\n");
			return L"Lucida Console";
		}
	}
	RegCloseKey(key);
	if (type == REG_MULTI_SZ) {
		for (i = 0; i < dlen/sizeof(wchar_t); i++) {
			if (data[i] == 0x0000)
				break;
		}
		if (i+1 >= dlen/sizeof(wchar_t) || data[i+1] == 0x0000) {
			printf("Bad registry value -- cannot find font\n");
			return L"Lucida Console";
		}
		i++;
	} else
	    i = 0;
	for (j = 0; i < dlen; i++, j++) {
		if (j == 0 && data[i] == L'*')
		    i++;
	    	if ((font[j] = data[i]) == 0x0000)
		    	break;
	}
	*codepage = _wtoi(cpname);
	return font;
}

/* Convert a hexadecimal digit to a nybble. */
static unsigned
hex(char c)
{
    	static char *digits = "0123456789abcdef";
	char *pos;

	pos = strchr(digits, c);
	if (pos == NULL)
	    	return 0; /* XXX */
	return pos - digits;
}

//#define DEBUG_EDIT 1

/*
 * Read an existing session file.
 * Returns 1 for success (file read and editable), 0 for failure.
 */
int
read_session(FILE *f, session_t *s)
{
    	char buf[1024];
	int saw_hex = 0;
	int saw_star = 0;
	unsigned long csum;
	unsigned long fcsum = 0;
	int ver;
	int s_off = 0;

	/*
	 * Look for the checksum and version.  Verify the version.
	 *
	 * XXX: It might be a good idea to validate each '!x' line and
	 * make sure that the length is right.
	 */
	while (fgets(buf, sizeof(buf), f) != NULL) {
	    	if (buf[0] == '!' && buf[1] == 'x')
		    	saw_hex = 1;
		else if (buf[0] == '!' && buf[1] == '*')
		    	saw_star = 1;
		else if (buf[0] == '!' && buf[1] == 'c') {
			if (sscanf(buf + 2, "%lx %d", &csum, &ver) != 2) {
#if defined(DEBUG_EDIT) /*[*/
				printf("[bad !c line '%s']\n", buf);
#endif /*]*/
				return 0;
			}
			if (ver > WIZARD_VER) {
#if defined(DEBUG_EDIT) /*[*/
				printf("[bad ver %d > %d]\n",
					ver, WIZARD_VER);
#endif /*]*/
			    	return 0;
			}
		}
	}
	if (!saw_hex || !saw_star) {
#if defined(DEBUG_EDIT) /*[*/
	    	printf("[missing%s%s]\n",
			saw_hex? "": "hex",
			saw_star? "": "star");
#endif /*]*/
		return 0;
	}

	/* Checksum from the top up to the '!c' line. */
	fflush(f);
	fseek(f, 0, SEEK_SET);
	fcsum = 0;
	while (fgets(buf, sizeof(buf), f) != NULL) {
	    	char *t;

		if (buf[0] == '!' && buf[1] == 'c')
		    	break;

		for (t = buf; *t; t++)
		    	fcsum += *t & 0xff;
	}
	if (fcsum != csum) {
#if defined(DEBUG_EDIT) /*[*/
	    	printf("[checksum mismatch, want 0x%08lx got 0x%08lx]\n",
			csum, fcsum);
#endif /*]*/
	    	return 0;
	}

	/* Once more, with feeling.  Scribble onto the session structure. */
	fflush(f);
	fseek(f, 0, SEEK_SET);
	s_off = 0;
	while (fgets(buf, sizeof(buf), f) != NULL) {

	    	if (buf[0] == '!' && buf[1] == 'x') {
		    	char *t;

			for (t = buf + 2; *t; t += 2) {
			    	if (*t == '\n')
				    	break;
				if (s_off > sizeof(*s)) {
#if defined(DEBUG_EDIT) /*[*/
					printf("[s overflow: %d > %d]\n",
						s_off, sizeof(*s));
#endif /*]*/
					return 0;
				}
			    	((char *)s)[s_off++] =
				    (hex(*t) << 4) | hex(*(t + 1));
			}
		} else if (buf[0] == '!' && buf[1] == 'c')
		    	break;
	}

	/*
	 * Read the balance of the file into a temporary buffer, ignoring
	 * the '!*' line.
	 */
	saw_star = 0;
	while (fgets(buf, sizeof(buf), f) != NULL) {
	    	if (!saw_star) {
			if (buf[0] == '!' && buf[1] == '*')
				saw_star = 1;
			continue;
		}
		if (user_settings == NULL) {
		    	user_settings = malloc(strlen(buf) + 1);
			user_settings[0] = '\0';
		} else
		    	user_settings = realloc(user_settings,
				strlen(user_settings) + strlen(buf) + 1);
		if (user_settings == NULL) {
#if defined(DEBUG_EDIT) /*[*/
			printf("out of memory]\n");
#endif /*]*/
			return 0;
		}
		strcat(user_settings, buf);
	}

	/* Success */
	return 1;
}

HRESULT
create_shortcut(session_t *session, char *exepath, char *linkpath,
	char *args, char *workingdir)
{
	wchar_t *font;
	int codepage = 0;

	font = reg_font_from_cset(session->charset, &codepage);

	return CreateLink(
		exepath,		/* path to executable */
		linkpath,		/* where to put the link */
		"wc3270 session",	/* description */
		args,			/* arguments */
		workingdir,		/* working directory */
		wrows[session->model], wcols[session->model],
					/* console rows, columns */
		font,			/* font */
		0,			/* point size */
		codepage);		/* code page */
}