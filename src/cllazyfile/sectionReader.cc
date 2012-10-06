#define SCL_CORE_EXPORT
#define SCL_DAI_EXPORT

#include <algorithm>
#include <assert.h>

#include "sdaiApplication_instance.h"
#include "sectionReader.h"
#include "lazyFileReader.h"
#include "lazyInstMgr.h"
#include "lazyTypes.h"

unsigned int sectionReader::_findStringBytes = 0; // incremented every time a byte is read in findNormalString()

sectionReader::sectionReader( lazyFileReader * parent, std::ifstream & file, std::streampos start ):
                                _lazyFile( parent ), _file( file ), _sectionStart( start ), _sectionID(-1) {
    _fileID = _lazyFile->ID();
}

std::streampos sectionReader::findNormalString( const std::string& str, bool semicolon, bool resetPos ) {
    std::streampos found = -1, startPos = _file.tellg(), nextTry = startPos;
    int i = 0, l = str.length();
    char c;

    //i is reset every time a character doesn't match; if i == l, this means that we've found the entire string
    while( i < l || semicolon ) {
        _file >> std::ws;
        c = _file.get();
        _findStringBytes++;

        if( ( i == l ) && ( semicolon ) ) {
            if( c == ';' ) {
                break;
            } else {
                i = 0;
                _file.seekg( nextTry );
                continue;
            }
        }
        if(  c == '\'' ) {
            //push past string
            _file.putback( c );
            ToExpressStr( _file, _lazyFile->getInstMgr()->getErrorDesc() );
        }
        if( ( c == '/' ) && ( _file.peek() == '*' ) ) {
            //push past comment
            findNormalString( "*/" );
        }
        if( str[i] == c ) {
            i++;
            if( i == 1 ) {
                nextTry = _file.tellg();
            }
        } else {
            if( !_file.good() ) {
                break;
            }
            if( i >= 1 ) {
                _file.seekg( nextTry );
            }
            i = 0;
        }
    }
    if( i == l ) {
            found = _file.tellg();
    }
    if( resetPos ) {
        _file.seekg( startPos );
    }
    if( _file.is_open() && _file.good() ) {
        return found;
    } else {
        return -1;
    }
}

//NOTE different behavior than const char * GetKeyword( istream & in, const char * delims, ErrorDescriptor & err ) in read_func.cc
std::string * sectionReader::getDelimitedKeyword( const char * delimiters ) {
    std::string * str = new std::string;
    char c;
    str->reserve(10); //seems to be faster and require less memory than no reserve or 20.
    _file >> ws;
    while( c = _file.get(), _file.good() ) {
        if( c == '-' || c == '_' || isupper( c ) || isdigit( c ) ||
            ( c == '!' && str->length() == 0 ) ) {
            str->append( 1, c );
        } else if( ( c == '/' ) && ( _file.peek() == '*' ) && ( str->length() == 0 ) ) {
            //push past comment
            findNormalString( "*/" );
            _file >> ws;
            continue;
        } else {
            _file.putback( c );
            break;
        }
    }
    c = _file.peek();
    if( !strchr( delimiters, c ) ) {
        std::cerr << __PRETTY_FUNCTION__ << ": missing delimiter. Found " << c << ", expected one of " << delimiters << " at end of keyword " << *str << ". File offset: " << _file.tellg() << std::endl;
        abort();
    }
    return str;
}

std::streampos sectionReader::seekInstanceEnd() {
    return findNormalString( ")", true );
}

void sectionReader::locateAllInstances() {
    namedLazyInstance inst;
    while( inst = nextInstance(), ( _file.good() ) && ( inst.loc.end < _sectionEnd ) ) {
        _lazyFile->getInstMgr()->addLazyInstance( inst );
    }
}

instanceID sectionReader::readInstanceNumber() {
    std::streampos hash,eq;
    char c;
    instanceID id = -1;

    //find instance number boundaries, check chars in between
    hash = findNormalString( "#" );
    eq = findNormalString( "=" );
    _file.seekg( hash );
    do {
        _file.get( c );
        if( !isdigit( c ) && ( c != ' ' ) && ( c != '\t' ) && ( c != '\n' ) ) {
            hash = findNormalString( "#" );
            if( hash > eq ) {
                eq = findNormalString( "=" );
                _file.seekg( hash );
            }
            _file >> ws;
        }
    } while( _file.tellg() < ( eq - 1L ) && _file.good() );
    if( _file.good() ) {
        _file.seekg( hash );
        _file >> ws;
        _file >> id;
    }
    return id;
}

//TODO: most of the rest of readdata1, all of readdata2
SDAI_Application_instance * sectionReader::getRealInstance( lazyInstanceLoc* inst ) {
//     assert( inst->instance == -1 );
    std::cerr << __PRETTY_FUNCTION__ << ": unimplemented. Instance #" << inst->instance << "." << std::endl;
    return 0;
}

#ifdef stepfile
/**
 * PASS 1:  create instances
 * starts at the data section
 */
int sectionReader::ReadData1( istream & in ) {
    int endsec = 0;
    _entsNotCreated = 0;

    _errorCount = 0;  // reset error count
    _warningCount = 0;  // reset error count

    char c;
    int instance_count = 0;
    char buf[BUFSIZ];
    buf[0] = '\0';
    std::string tmpbuf;

    SDAI_Application_instance * obj = ENTITY_NULL;
    stateEnum inst_state = noStateSE; // used if reading working file

    ErrorDescriptor e;

    //  PASS 1:  create instances
    endsec = FoundEndSecKywd( in, _error );
    while( in.good() && !endsec ) {
        e.ClearErrorMsg();
        ReadTokenSeparator( in ); // also skips white space
        in >> c;

        if( _fileType == WORKING_SESSION ) {
            if( strchr( "CIND", c ) ) { // if there is a valid char
                inst_state = EntityWfState( c );
                ReadTokenSeparator( in );
                in >> c;    // read the ENTITY_NAME_DELIM
            } else {
                e.AppendToDetailMsg( "Invalid editing state character: " );
                e.AppendToDetailMsg( c );
                e.AppendToDetailMsg( "\nAssigning editing state to be INCOMPLETE\n" );
                e.GreaterSeverity( SEVERITY_WARNING );
                inst_state = incompleteSE;
            }
        }

        if( c != ENTITY_NAME_DELIM ) {
            in.putback( c );
            while( c != ENTITY_NAME_DELIM && in.good() &&
                !( endsec = FoundEndSecKywd( in, _error ) ) ) {
                tmpbuf.clear();
            FindStartOfInstance( in, tmpbuf );
            cout << "ERROR: trying to recover from invalid data. skipping: "
            << tmpbuf << endl;
            in >> c;
            ReadTokenSeparator( in );
                }
        }

        if( !endsec ) {
            obj = ENTITY_NULL;
            if( ( _fileType == WORKING_SESSION ) && ( inst_state == deleteSE ) ) {
                SkipInstance( in, tmpbuf );
            } else {
                obj =  CreateInstance( in, cout );
                _iFileCurrentPosition = in.tellg();
            }

            if( obj != ENTITY_NULL ) {
                if( obj->Error().severity() < SEVERITY_WARNING ) {
                    ++_errorCount;
                } else if( obj->Error().severity() < SEVERITY_NULL ) {
                    ++_warningCount;
                }
                obj->Error().ClearErrorMsg();

                if( _fileType == WORKING_SESSION ) {
                    instances().Append( obj, inst_state );
                } else {
                    instances().Append( obj, newSE );
                }

                ++instance_count;
            } else {
                ++_entsNotCreated;
                //old
                ++_errorCount;
            }

            if( _entsNotCreated > _maxErrorCount ) {
                _error.AppendToUserMsg( "Warning: Too Many Errors in File. Read function aborted.\n" );
                cerr << Error().UserMsg();
                cerr << Error().DetailMsg();
                Error().ClearErrorMsg();
                Error().severity( SEVERITY_EXIT );
                return instance_count;
            }

            endsec = FoundEndSecKywd( in, _error );

        }
    } // end while loop

    if( _entsNotCreated ) {
        sprintf( buf,
                 "sectionReader Reading File: Unable to create %d instances.\n\tIn first pass through DATA section. Check for invalid entity types.\n",
                 _entsNotCreated );
        _error.AppendToUserMsg( buf );
        _error.GreaterSeverity( SEVERITY_WARNING );
    }
    if( !in.good() ) {
        _error.AppendToUserMsg( "Error in input file.\n" );
    }

    _iFileStage1Done = true;
    return instance_count;
}

/**
 * \returns number of valid instances read
 * reads in the data portion of the instances in an exchange file
 */
int sectionReader::ReadData2( istream & in, bool useTechCor ) {
    _entsInvalid = 0;
    _entsIncomplete = 0;
    _entsWarning = 0;

    int total_instances = 0;
    int valid_insts = 0;    // used for exchange file only
    stateEnum inst_state = noStateSE; // used if reading working file

    _errorCount = 0;  // reset error count
    _warningCount = 0;  // reset error count

    char c;
    char buf[BUFSIZ];
    buf[0] = '\0';
    std::string tmpbuf;

    SDAI_Application_instance * obj = ENTITY_NULL;
    std::string cmtStr;

    //    ReadTokenSeparator(in, &cmtStr);

    int endsec = FoundEndSecKywd( in, _error );

    //  PASS 2:  read instances
    while( in.good() && !endsec ) {
        ReadTokenSeparator( in, &cmtStr );
        in >> c;

        if( _fileType == WORKING_SESSION ) {
            if( strchr( "CIND", c ) ) { // if there is a valid char
                inst_state = EntityWfState( c );
                ReadTokenSeparator( in, &cmtStr );
                in >> c;    // read the ENTITY_NAME_DELIM
            }
            /*
             *                    // don't need this error msg for the 2nd pass (it was done on 1st)
             *                    else
             *                    {
             *                    cout << "Invalid editing state character: " << c << endl;
             *                    cout << "Assigning editing state to be INCOMPLETE\n";
             *                    inst_state = incompleteSE;
             }
             */
             }

             if( c != ENTITY_NAME_DELIM ) {
                 in.putback( c );
                 while( c != ENTITY_NAME_DELIM && in.good() &&
                     !( endsec = FoundEndSecKywd( in, _error ) ) ) {

                     tmpbuf.clear();
                 FindStartOfInstance( in, tmpbuf );
                 cout << "ERROR: trying to recover from invalid data. skipping: "
                 << tmpbuf << endl;
                 in >> c;
                 ReadTokenSeparator( in, &cmtStr );
                     }
             }


             if( !endsec ) {
                 obj = ENTITY_NULL;
                 if( ( _fileType == WORKING_SESSION ) && ( inst_state == deleteSE ) ) {
                     SkipInstance( in, tmpbuf );
                 } else {
                     obj =  ReadInstance( in, cout, cmtStr, useTechCor );
                     _iFileCurrentPosition = in.tellg();
                 }

                 cmtStr.clear();
                 if( obj != ENTITY_NULL ) {
                     if( obj->Error().severity() < SEVERITY_INCOMPLETE ) {
                         ++_entsInvalid;
                         // old
                         ++_errorCount;
                     } else if( obj->Error().severity() == SEVERITY_INCOMPLETE ) {
                         ++_entsIncomplete;
                         ++_entsInvalid;
                     } else if( obj->Error().severity() == SEVERITY_USERMSG ) {
                         ++_entsWarning;
                     } else { // i.e. if severity == SEVERITY_NULL
                    ++valid_insts;
                 }

                 obj->Error().ClearErrorMsg();

                 ++total_instances;
             } else {
                 ++_entsInvalid;
                 // old
                 ++_errorCount;
             }

             if( _entsInvalid > _maxErrorCount ) {
                 _error.AppendToUserMsg( "Warning: Too Many Errors in File. Read function aborted.\n" );
                 cerr << Error().UserMsg();
                 cerr << Error().DetailMsg();
                 Error().ClearErrorMsg();
                 Error().severity( SEVERITY_EXIT );
                 return valid_insts;
             }

             endsec = FoundEndSecKywd( in, _error );
        }
    } // end while loop

    if( _entsInvalid ) {
        sprintf( buf,
                 "%s \n\tTotal instances: %d \n\tInvalid instances: %d \n\tIncomplete instances (includes invalid instances): %d \n\t%s: %d.\n",
                 "Second pass complete - instance summary:", total_instances,
                 _entsInvalid, _entsIncomplete, "Warnings",
                 _entsWarning );
        cout << buf << endl;
        _error.AppendToUserMsg( buf );
        _error.AppendToDetailMsg( buf );
        _error.GreaterSeverity( SEVERITY_WARNING );
    }
    if( !in.good() ) {
        _error.AppendToUserMsg( "Error in input file.\n" );
    }

    //    if( in.good() )
    //  in.putback(c);
    return valid_insts;
}

#endif //stepfile