/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014-2020 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2007-2015 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2008 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright (C) 1992-2020 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * The common library
 * @file common.h
 */

#ifndef INCLUDE__COMMON_H_
#define INCLUDE__COMMON_H_

#include <vector>
#include <functional>

#include <wx/confbase.h>
#include <wx/fileconf.h>
#include <wx/dir.h>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/process.h>

#include <atomic>
#include <limits>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <macros.h>

class PROJECT;
class SEARCH_STACK;
class REPORTER;

/**
 * Run a command in a child process.
 *
 * @param aCommandLine The process and any arguments to it all in a single
 *                     string.
 * @param aFlags The same args as allowed for wxExecute()
 * @param callback wxProcess implementing OnTerminate to be run when the
                   child process finishes
 * @return int - pid of process, 0 in case of error (like return values of
 *               wxExecute())
 */
int ProcessExecute( const wxString& aCommandLine, int aFlags = wxEXEC_ASYNC,
                    wxProcess *callback = NULL );

/**
 * Return the help file's full path.
 * <p>
 * Return the KiCad help file with path and extension.
 * Help files can be html (.html ext) or pdf (.pdf ext) files.
 * A \<BaseName\>.html file is searched and if not found,
 * \<BaseName\>.pdf file is searched in the same path.
 * If the help file for the current locale is not found, an attempt to find
 * the English version of the help file is made.
 * Help file is searched in directories in this order:
 *  help/\<canonical name\> like help/en_GB
 *  help/\<short name\> like help/en
 *  help/en
 * </p>
 * @param aSearchStack contains some possible base dirs that may be above the
 *  the one actually holding @a aBaseName.  These are starting points for nested searches.
 * @param aBaseName is the name of the help file to search for, <p>without extension</p>.
 * @return  wxEmptyString is returned if aBaseName is not found, else the full path & filename.
 */
wxString SearchHelpFileFullPath( const SEARCH_STACK& aSearchStack, const wxString& aBaseName );

/**
 * Make \a aTargetFullFileName absolute and create the path of this file if it doesn't yet exist.
 *
 * @param aTargetFullFileName the wxFileName containing the full path and file name to modify.
 *                            The path may be absolute or relative to \a aBaseFilename .
 * @param aBaseFilename a full filename. Only its path is used to set the aTargetFullFileName path.
 * @param aReporter a point to a REPORTER object use to show messages (can be NULL)
 * @return true if \a aOutputDir already exists or was successfully created.
 */
bool EnsureFileDirectoryExists( wxFileName*     aTargetFullFileName,
                                const wxString& aBaseFilename,
                                REPORTER*       aReporter = NULL );

/**
 * Replace any environment variable & text variable references with their values.
 *
 * @param aString = a string containing (perhaps) references to env var
 * @return a string where env var are replaced by their value
 */
const wxString ExpandEnvVarSubstitutions( const wxString& aString, PROJECT* aProject );

/**
 * Expand '${var-name}' templates in text.  The LocalResolver is given first crack at it,
 * after which the PROJECT's resolver is called.
 */
wxString ExpandTextVars( const wxString& aSource,
                         const std::function<bool( wxString* )>* aLocalResolver,
                         const PROJECT* aProject,
                         const std::function<bool( wxString* )>* aFallbackResolver = nullptr );

/**
 * Replace any environment and/or text variables in file-path uris (leaving network-path URIs
 * alone).
 */
const wxString ResolveUriByEnvVars( const wxString& aUri, PROJECT* aProject );


#ifdef __WXMAC__
/**
 * OSX specific function GetOSXKicadUserDataDir
 * @return A wxString pointing to the user data directory for Kicad
 */
wxString GetOSXKicadUserDataDir();

/**
 * OSX specific function GetOSXMachineDataDir
 * @return A wxString pointing to the machine data directory for Kicad
 */
wxString GetOSXKicadMachineDataDir();

/**
 * OSX specific function GetOSXKicadDataDir
 * @return A wxString pointing to the bundle data directory for Kicad
 */
wxString GetOSXKicadDataDir();
#endif

// Some wxWidgets versions (for instance before 3.1.0) do not include
// this function, so add it if missing
#if !wxCHECK_VERSION( 3, 1, 0 )
#define USE_KICAD_WXSTRING_HASH     // for common.cpp
///> Template specialization to enable wxStrings for certain containers (e.g. unordered_map)
namespace std
{
    template<> struct hash<wxString>
    {
        size_t operator()( const wxString& s ) const;
    };
}
#endif

/// Required to use wxPoint as key type in maps
#define USE_KICAD_WXPOINT_LESS_AND_HASH // for common.cpp
namespace std
{
    template <> struct hash<wxPoint>
    {
        size_t operator() ( const wxPoint& k ) const;
    };
}

namespace std
{
    template<> struct less<wxPoint>
    {
        bool operator()( const wxPoint& aA, const wxPoint& aB ) const;
    };
}

/**
 * Helper function to print the given wxSize to a stream.
 *
 * Used for debugging functions like EDA_ITEM::Show and also in unit
 * testing fixtures.
 */
std::ostream& operator<<( std::ostream& out, const wxSize& size );

/**
 * Helper function to print the given wxPoint to a stream.
 *
 * Used for debugging functions like EDA_ITEM::Show and also in unit
 * testing fixtures.
 */
std::ostream& operator<<( std::ostream& out, const wxPoint& pt );

long long TimestampDir( const wxString& aDirPath, const wxString& aFilespec );


#endif  // INCLUDE__COMMON_H_
