

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 06:14:07 2038
 */
/* Compiler settings for Praktika_Api.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __Praktika_Api_h__
#define __Praktika_Api_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __Praktika_Api_INTERFACE_DEFINED__
#define __Praktika_Api_INTERFACE_DEFINED__

/* interface Praktika_Api */
/* [unique][endpoint][version][uuid] */ 

long GetAuthInfo( 
    /* [in] */ handle_t hBinding,
    /* [in] */ long bufLen,
    /* [size_is][out] */ wchar_t username[  ]);

long Login( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *username,
    /* [string][in] */ const wchar_t *password);

long Logout( 
    /* [in] */ handle_t hBinding);

long GetLicenseInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ long *pIsLicensed,
    /* [in] */ long expiryBufLen,
    /* [size_is][out] */ wchar_t expiryDate[  ]);

long ActivateProduct( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *activationCode);

long GetAvDbInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ long *pLoaded,
    /* [out] */ long *pRecordCount,
    /* [in] */ long dateBufLen,
    /* [size_is][out] */ wchar_t releaseDate[  ]);

long ScanFilePath( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *filePath,
    /* [out] */ long *pIsThreat,
    /* [in] */ long nameBufLen,
    /* [size_is][out] */ wchar_t threatName[  ]);

long ScanDirPath( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ const wchar_t *dirPath,
    /* [out] */ long *pTotalFiles,
    /* [out] */ long *pScannedFiles,
    /* [out] */ long *pThreatsFound,
    /* [in] */ long listBufLen,
    /* [size_is][out] */ wchar_t threatList[  ]);



extern RPC_IF_HANDLE Praktika_Api_v1_0_c_ifspec;
extern RPC_IF_HANDLE Praktika_Api_v1_0_s_ifspec;
#endif /* __Praktika_Api_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


