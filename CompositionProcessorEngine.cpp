﻿//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Private.h"
#include "TSFTTS.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "DictionarySearch.h"
#include "Globals.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "sddl.h"



//////////////////////////////////////////////////////////////////////
//
// CompositionProcessorEngine implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::CCompositionProcessorEngine(_In_ CTSFTTS *pTextService)
{
	_pTextService = pTextService;
    _pTextService->AddRef();

    _pTableDictionaryEngine = nullptr;
	_pCINTableDictionaryEngine = nullptr;
	_pTTSTableDictionaryEngine = nullptr;

    _pTTSDictionaryFile = nullptr;
	_pCINDictionaryFile = nullptr;

   
    _tfClientId = TF_CLIENTID_NULL;

	
 
    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    _isWildcard = FALSE;
    _isDisableWildcardAtFirst = FALSE;
    _hasMakePhraseFromText = FALSE;
    _isKeystrokeSort = FALSE;

	

	_candidateWndWidth = 5;  //default with =  3 charaters + 2 trailling space

    _candidateListPhraseModifier = 0;

	InitKeyStrokeTable();

}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::~CCompositionProcessorEngine()
{
	_pTextService->Release();
    if (_pTTSTableDictionaryEngine)
    {
        delete _pTTSTableDictionaryEngine;
        _pTTSTableDictionaryEngine = nullptr;
	}
	if (_pCINTableDictionaryEngine)
    {
        delete _pCINTableDictionaryEngine;
        _pCINTableDictionaryEngine = nullptr;
	}
	_pTableDictionaryEngine = nullptr;

	

	if (_pTTSDictionaryFile)
    {
        delete _pTTSDictionaryFile;
        _pTTSDictionaryFile = nullptr;
    }

    if (_pCINDictionaryFile)
    {
        delete _pCINDictionaryFile;
        _pCINDictionaryFile = nullptr;
    }
}


//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
    if (!wch)
    {
        return FALSE;
    }
	if((UINT)_keystrokeBuffer.GetLength() >= _pTextService->GetMaxCodes() )  // do not eat the key if keystroke buffer length >= _maxcodes
	{
		DoBeep();
		return FALSE;
	}
    //
    // append one keystroke in buffer.
    //
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen + 1 ];
    if (!pwch)
    {
        return FALSE;
    }

    memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
    pwch[ srgKeystrokeBufLen ] = wch;

    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
    }

    _keystrokeBuffer.Set(pwch, srgKeystrokeBufLen + 1);

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

    if (dwIndex + 1 < srgKeystrokeBufLen)
    {
        // shift following eles left
        memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
            (BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
            (srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
    }

    _keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);
}

//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
    if ( _keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
        _keystrokeBuffer.Set(NULL, 0);
    }
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex) 
{ 
    if (dwIndex < _keystrokeBuffer.GetLength())
    {
        return *(_keystrokeBuffer.Get() + dwIndex);
    }
    return 0;
}
//+---------------------------------------------------------------------------
//
// GetReadingStrings
// Retrieves string from Composition Processor Engine.
// param
//     [out] pReadingStrings - Specified returns pointer of CUnicodeString.
// returns
//     none
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetReadingStrings(_Inout_ CTSFTTSArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded)
{
    CStringRange oneKeystroke;

    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    if (pReadingStrings->Count() == 0 && _keystrokeBuffer.GetLength())
    {
        CStringRange* pNewString = nullptr;

        pNewString = pReadingStrings->Append();
        if (pNewString)
        {
            *pNewString = _keystrokeBuffer;
        }

		PWCHAR pwchRadical;
		pwchRadical = new (std::nothrow) WCHAR[_keystrokeBuffer.GetLength() + 1];
		*pwchRadical = L'\0';
		
        for (DWORD index = 0; index < _keystrokeBuffer.GetLength(); index++)
        {
			if(Global::radicalMap.size() && !IsSymbol()) // if radicalMap is valid (size()>0), then convert the keystroke buffer 
			{
				WCHAR* radicalChar = new (std::nothrow) WCHAR[2];
				*radicalChar = towupper(*(_keystrokeBuffer.Get() + index));
				WCHAR* radical = &Global::radicalMap[*radicalChar];
				if(*radical == L'\0') *radical = *radicalChar;
				StringCchCatN(pwchRadical, _keystrokeBuffer.GetLength() + 1, radical,1); 
			}

            oneKeystroke.Set(_keystrokeBuffer.Get() + index, 1);

            if (IsWildcard() && IsWildcardChar(*oneKeystroke.Get()))
            {
                _hasWildcardIncludedInKeystrokeBuffer = TRUE;
            }
        }
		if(Global::radicalMap.size()&& !IsSymbol())
		{
			pNewString->Set(pwchRadical, _keystrokeBuffer.GetLength());
		}
		else
		{
			delete [] pwchRadical;
		}
    }

    *pIsWildcardIncluded = _hasWildcardIncludedInKeystrokeBuffer;
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch)
{
    if (!IsDictionaryAvailable())
    {
        return;
    }

    if (isIncrementalWordSearch)
    {
        CStringRange wildcardSearch;
        DWORD_PTR keystrokeBufLen = _keystrokeBuffer.GetLength() + 3;
       

        // check keystroke buffer already has wildcard char which end user want wildcard serach
        DWORD wildcardIndex = 0;
        BOOL isFindWildcard = FALSE;

        if (IsWildcard())
        {
            for (wildcardIndex = 0; wildcardIndex < _keystrokeBuffer.GetLength(); wildcardIndex++)
            {
                if (IsWildcardChar(*(_keystrokeBuffer.Get() + wildcardIndex)))
                {
                    isFindWildcard = TRUE;
                    break;
                }
            }
        }

        PWCHAR pwch = new (std::nothrow) WCHAR[ keystrokeBufLen ];
        if (!pwch)  return;
		PWCHAR pwch3code = new (std::nothrow) WCHAR[ 6 ];
		if (!pwch3code) return;

		if (!isFindWildcard  && (_pTextService->GetThreeCodeMode() && _keystrokeBuffer.GetLength() == 3))
        {
			StringCchCopyN(pwch, keystrokeBufLen, _keystrokeBuffer.Get(), _keystrokeBuffer.GetLength());
			StringCchCat(pwch, keystrokeBufLen, L"*");
			size_t len = 0;
			if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) == S_OK)
					wildcardSearch.Set(pwch, len);
			

			pwch3code[0] = *(_keystrokeBuffer.Get()); pwch3code[1] = *(_keystrokeBuffer.Get()+1);       
			pwch3code[2] = L'*';
			pwch3code[3] = *(_keystrokeBuffer.Get()+2);    
			pwch3code[4] = L'*'; pwch3code[5] = L'\0'; 
			
			CTSFTTSArray<CCandidateListItem> wCandidateList;
			CStringRange w3codeMode;			
			_pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, pCandidateList);
			if(pCandidateList->Count())	
			{
				_pTableDictionaryEngine->CollectWordForWildcard(&w3codeMode.Set(pwch3code,4), &wCandidateList);
				if(wCandidateList.Count())
				{   //append the candidate items got with 3codemode wildcard string to the end of exact match items.
					for(UINT i = 0; i < wCandidateList.Count(); i++)
					{
						if(!CStringRange::WildcardCompare(_pTextService->GetLocale(), &wildcardSearch, &(wCandidateList.GetAt(i)->_FindKeyCode)))
						{
							CCandidateListItem* pLI = nullptr;
							pLI = pCandidateList->Append();
							if (pLI)
							{
								pLI->_ItemString.Set(wCandidateList.GetAt(i)->_ItemString);
								pLI->_FindKeyCode.Set(wCandidateList.GetAt(i)->_FindKeyCode);
							}
						}
					}
					wCandidateList.Clear();
				}
			}
			else
			{
				_pTableDictionaryEngine->CollectWordForWildcard(&w3codeMode.Set(pwch3code,4), pCandidateList);
			}


        }
		else    // add wildcard char for incremental search
		{
			StringCchCopyN(pwch, keystrokeBufLen, _keystrokeBuffer.Get(), _keystrokeBuffer.GetLength());
			if(!isFindWildcard)
			{
				StringCchCat(pwch, keystrokeBufLen, L"*");
			}

			size_t len = 0;
			if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) == S_OK)
			{
				wildcardSearch.Set(pwch, len);
			}
			else
			{
		        return;
	        }

			_pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		}

        

        if (0 >= pCandidateList->Count())
        {
            return;
        }

        if (IsKeystrokeSort())
        {
            _pTableDictionaryEngine->SortListItemByFindKeyCode(pCandidateList);
        }

        // Incremental search would show keystroke data from all candidate list items
        // but wont show identical keystroke data for user inputted.
        for (UINT index = 0; index < pCandidateList->Count(); index++)
        {
            CCandidateListItem *pLI = pCandidateList->GetAt(index);
            DWORD_PTR keystrokeBufferLen = 0;

            if (IsWildcard())
            {
                keystrokeBufferLen = wildcardIndex;
            }
            else
            {
                keystrokeBufferLen = _keystrokeBuffer.GetLength();
            }

            CStringRange newFindKeyCode;
            newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keystrokeBufferLen, pLI->_FindKeyCode.GetLength() - keystrokeBufferLen);
            pLI->_FindKeyCode.Set(newFindKeyCode);
        }

        delete [] pwch;
		delete [] pwch3code;
    }
    else if (isWildcardSearch)
    {
        _pTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
    }
	else if (_pTextService->GetThreeCodeMode() && _keystrokeBuffer.GetLength() == 3)
	{
		

		CStringRange wildcardSearch;
        PWCHAR pwch = new (std::nothrow) WCHAR[ 5 ];
        if (!pwch) return;
		pwch[0] = *(_keystrokeBuffer.Get());	pwch[1] = *(_keystrokeBuffer.Get()+1);       
		pwch[2] = L'?';		pwch[3] = *(_keystrokeBuffer.Get()+2);      
		wildcardSearch.Set(pwch, 4);
		CTSFTTSArray<CCandidateListItem> wCandidateList;

		_pTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
		if(pCandidateList->Count())
		{
			_pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, &wCandidateList);
			if(wCandidateList.Count())
			{   //append the candidate items got with 3codemode wildcard string to the end of exact match items.
				for(UINT i = 0; i < wCandidateList.Count(); i++)
				{
					if(!(CStringRange::Compare(_pTextService->GetLocale(), &_keystrokeBuffer, &(wCandidateList.GetAt(i)->_FindKeyCode)) == CSTR_EQUAL))
					{
						CCandidateListItem* pLI = nullptr;
						pLI = pCandidateList->Append();
						if (pLI)
						{
							pLI->_ItemString.Set(wCandidateList.GetAt(i)->_ItemString);
							pLI->_FindKeyCode.Set(wCandidateList.GetAt(i)->_FindKeyCode);
						}
					}
				}
				wCandidateList.Clear();
			}
		}
		else //if no exact match items found, send the results from 3codemode 
		{
			_pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		}
		delete [] pwch;
	}
    else 
    {
        _pTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
    }

    for (UINT index = 0; index < pCandidateList->Count();)
    {
        CCandidateListItem *pLI = pCandidateList->GetAt(index);
        CStringRange startItemString;
        CStringRange endItemString;

        startItemString.Set(pLI->_ItemString.Get(), 1);
        endItemString.Set(pLI->_ItemString.Get() + pLI->_ItemString.GetLength() - 1, 1);

		if(pLI->_ItemString.GetLength() > _candidateWndWidth - 2 )
		{
			_candidateWndWidth = (UINT) pLI->_ItemString.GetLength() + 2;
		}
		/*
		WCHAR debugStr[256];
		StringCchPrintf(debugStr, 256, L"cand item - %d : length: %d \n", index, pLI->_ItemString.GetLength());
		debugPrint(debugStr);
		*/
        index++;
    }
}

//+---------------------------------------------------------------------------
//
// GetCandidateStringInConverted
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateStringInConverted(CStringRange &searchString, _In_ CTSFTTSArray<CCandidateListItem> *pCandidateList)
{
    if (!IsDictionaryAvailable())
    {
        return;
    }

    // Search phrase from SECTION_TEXT's converted string list
    CStringRange searchText;
    DWORD_PTR srgKeystrokeBufLen = searchString.GetLength() + 2;
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen ];
    if (!pwch)
    {
        return;
    }

    StringCchCopyN(pwch, srgKeystrokeBufLen, searchString.Get(), searchString.GetLength());
	if(!(Global::hasPhraseSection || Global::hasCINPhraseSection)) StringCchCat(pwch, srgKeystrokeBufLen, L"*");  // do wild search if no TTS [Phrase]section present

    // add wildcard char
	size_t len = 0;
	if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) != S_OK)
    {
        return;
    }
    searchText.Set(pwch, len);

	if(_pCINTableDictionaryEngine && Global::hasCINPhraseSection) // do phrase lookup if CIN file has phrase section
		_pCINTableDictionaryEngine->CollectWordFromConvertedString(&searchText, pCandidateList);
	else if(_pTTSTableDictionaryEngine && Global::hasPhraseSection)// do phrase lookup in TTS file if CIN phrase section is not present
		_pTTSTableDictionaryEngine->CollectWordFromConvertedString(&searchText, pCandidateList);
	else // no phrase section, do wildcard text search
		_pTableDictionaryEngine->CollectWordFromConvertedStringForWildcard(&searchText, pCandidateList);

	if (IsKeystrokeSort())
		_pTableDictionaryEngine->SortListItemByFindKeyCode(pCandidateList);
		
	for (UINT index = 0; index < pCandidateList->Count();)
    {
        CCandidateListItem *pLI = pCandidateList->GetAt(index);
        
		if(pLI->_ItemString.GetLength() > _candidateWndWidth - 2 )
		{
			_candidateWndWidth = (UINT) pLI->_ItemString.GetLength() + 2;
		}
		
	    index++;
    }

    searchText.Clear();
    delete [] pwch;
}

//+---------------------------------------------------------------------------
//
// IsSymbol
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbol()
{
	return (_keystrokeBuffer.GetLength()<3 && *_keystrokeBuffer.Get()==L'=');	
}

//+---------------------------------------------------------------------------
//
// IsSymbolChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolChar(WCHAR wch)
{
	if(_keystrokeBuffer.Get() == nullptr) return FALSE;
	if((_keystrokeBuffer.GetLength() == 1) && (*_keystrokeBuffer.Get() == L'=') ) 
	{
		for (int i = 0; i < ARRAYSIZE(Global::symbolCharTable); i++)
		{
			if (Global::symbolCharTable[i] == wch)
			{
				return TRUE;
			}
		}

	}
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsAddressChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsAddressChar(WCHAR wch)
{
	if(_keystrokeBuffer.Get() == nullptr || (_keystrokeBuffer.Get() && (_keystrokeBuffer.GetLength() == 0))) 
	{
		for (int i = 0; i < ARRAYSIZE(Global::addressCharTable); i++)
		{
			if (Global::addressCharTable[i]._Code == wch)
			{
				return TRUE;
			}
		}

	}
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// GetAddressChar
//
//----------------------------------------------------------------------------

WCHAR CCompositionProcessorEngine::GetAddressChar(WCHAR wch)
{
    for (int i = 0; i < ARRAYSIZE(Global::addressCharTable); i++)
    {
        if (Global::addressCharTable[i]._Code == wch)
        {
			return Global::addressCharTable[i]._AddressChar;
        }
    }
	return 0;
}



//+---------------------------------------------------------------------------
//
// IsDoubleSingleByte
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsDoubleSingleByte(WCHAR wch)
{
    if (L' ' <= wch && wch <= L'~')
    {
        return TRUE;
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke()
{
    SetKeystrokeTable(&_KeystrokeComposition);
    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetKeystrokeTable(_Inout_ CTSFTTSArray<_KEYSTROKE> *pKeystroke)
{

    for (int i = 0; i <  41; i++)
    {
        _KEYSTROKE* pKS = nullptr;

        pKS = pKeystroke->Append();
        if (!pKS)
        {
            break;
        }
        *pKS = _keystrokeTable[i];
    }
}

//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    TF_PRESERVEDKEY preservedKeyImeMode;
    preservedKeyImeMode.uVKey = VK_SHIFT;
    preservedKeyImeMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;

	
    SetPreservedKey(Global::TSFTTSGuidImeModePreserveKey, preservedKeyImeMode, Global::ImeModeDescription, &_PreservedKey_IMEMode);

    TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
    preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
    preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
    SetPreservedKey(Global::TSFTTSGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);

    InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);

    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey)
{
    pXPreservedKey->Guid = clsid;

    TF_PRESERVEDKEY *ptfPsvKey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
    if (!ptfPsvKey1)
    {
        return;
    }
    *ptfPsvKey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
    {
        return;
    }
    pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
    if (!pXPreservedKey->Description)
    {
        return;
    }

    StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

    return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

    if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK)
    {
        return FALSE;
    }

    for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
        preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
        {
            return FALSE;
        }
        pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CTSFTTSArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
    for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
    {
        TF_PRESERVEDKEY *ptfPsvKey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly)         )
        {
            return FALSE;
        }
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	debugPrint(L"CCompositionProcessorEngine::OnPreservedKey() \n");
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isOpen = FALSE;
		
        
		if(Global::isWindows8){
			isOpen = FALSE;
			CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
			CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
			CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}
		else
		{
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
			CompartmentIMEMode._GetCompartmentBOOL(isOpen);
			CompartmentIMEMode._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}

		
		

        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isDouble = FALSE;
        CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
        CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
        CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
        *pIsEaten = TRUE;
    }
   
    else
    {
        *pIsEaten = FALSE;
    }
    *pIsEaten = TRUE;
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration()
{
    _isWildcard = TRUE;
    _isDisableWildcardAtFirst = TRUE;
    _hasMakePhraseFromText = TRUE;
    _isKeystrokeSort = FALSE;

   _candidateWndWidth = 5;
    SetInitialCandidateListRange();


    return;
}


//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile()
{	
    debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() \n");

	
	WCHAR wszProgramFiles[MAX_PATH];
	WCHAR wszAppData[MAX_PATH];

	/*
	//LPWSTR pwzProgramFiles = wszProgramFiles;
	WCHAR wszSysWOW64[MAX_PATH];
	if(GetSystemWow64Directory(wszSysWOW64, MAX_PATH)>0){ //return 0 indicates x86 system, x64 otherwize.
	//x64 system.  Use ProgramW6432 environment variable to get %SystemDrive%\Program Filess.
		//GetEnvironmentVariable(L"ProgramW6432", pwzProgramFiles, MAX_PATH);  //W6432 does not exist on VISTA
		GetEnvironmentVariable(L"ProgramFiles", pwzProgramFiles, MAX_PATH);
	}else
	{//x86 system. 
		SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &pwzProgramFiles);
	}
	//CSIDL_APPDATA  personal roadming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);
	*/
	// the environment variable consistenly get "Program Files" directory in either x64 or x86 system.
	GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH); 
	
	WCHAR wzsTTSFileName[MAX_PATH] = L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt";
	WCHAR wzsTSFTTSProfile[MAX_PATH] = L"\\TSFTTS";
	WCHAR wzsCINFileName[MAX_PATH] = L"\\Dayi.cin";

    WCHAR *pwszFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR *pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
	
    if (!pwszFileName)  goto ErrorExit;
	if (!pwszCINFileName)  goto ErrorExit;

	*pwszFileName = L'\0';
	*pwszCINFileName = L'\0';

	//tableTextService (TTS) dictionary file 
	StringCchCopyN(pwszFileName, MAX_PATH, wszProgramFiles, wcslen(wszProgramFiles) + 1);
	StringCchCatN(pwszFileName, MAX_PATH, wzsTTSFileName, wcslen(wzsTTSFileName));

	//create CFileMapping object
    if (_pTTSDictionaryFile == nullptr)
    {
        _pTTSDictionaryFile = new (std::nothrow) CFileMapping();
        if (!_pTTSDictionaryFile)  goto ErrorExit;
    }
	if (!(_pTTSDictionaryFile)->CreateFile(pwszFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
	{
		goto ErrorExit;
	}
	else
	{
		_pTTSTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTTSDictionaryFile, L'=', _pTextService); //TTS file use '=' as delimiter
		if (!_pTTSTableDictionaryEngine)  goto ErrorExit;
		_pTTSTableDictionaryEngine->ParseConfig(); //parse config first.
		_pTableDictionaryEngine = _pTTSTableDictionaryEngine;  //set TTS as default dictionary engine
	}

	
	StringCchCopyN(pwszCINFileName, MAX_PATH, wszAppData, wcslen(wszAppData));
	StringCchCatN(pwszCINFileName, MAX_PATH, wzsTSFTTSProfile, wcslen(wzsTSFTTSProfile));
	if(PathFileExists(pwszCINFileName))
	{ //dayi.cin in personal romaing profile
		StringCchCatN(pwszCINFileName, MAX_PATH, wzsCINFileName, wcslen(wzsCINFileName));
		if(PathFileExists(pwszCINFileName))  //create cin CFileMapping object
		{
			 //create CFileMapping object
			if (_pCINDictionaryFile == nullptr)
			{
				_pCINDictionaryFile = new (std::nothrow) CFileMapping();
				if ((_pCINDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
				{
					_pCINTableDictionaryEngine = new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pCINDictionaryFile, L'\t', _pTextService); //cin files use tab as delimiter

					if (_pCINTableDictionaryEngine)  
					{
						_pCINTableDictionaryEngine->ParseConfig(); //parse config first.
						_pTableDictionaryEngine = _pCINTableDictionaryEngine;  //set CIN as dictionary engine if avaialble
					}
				}
			}
			
		}
		
	}
	else
	{
		//TSFTTS roadming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);	
	}
	
  
    delete []pwszFileName;
	delete []pwszCINFileName;
    return TRUE;
ErrorExit:
    if (pwszFileName)  delete []pwszFileName;
    if (pwszCINFileName)  delete []pwszCINFileName;
    return FALSE;
}




//+---------------------------------------------------------------------------
//
// GetDictionaryFile
//
//----------------------------------------------------------------------------

CFile* CCompositionProcessorEngine::GetDictionaryFile()
{
    return _pTTSDictionaryFile;
}



//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr)
{
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if (IsEqualGUID(Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return FALSE;
    }

    for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
        pPreservedKey.uModifiers &= 0xffff;

        pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
    Guid = GUID_NULL;
    Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
    ITfThreadMgr* pThreadMgr = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        UninitPreservedKey(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (Description)
    {
        delete [] Description;
    }
}


void CCompositionProcessorEngine::InitKeyStrokeTable()
{
	for (int i = 0; i < 10; i++)
    {
        _keystrokeTable[i].VirtualKey = '0' + i;
        _keystrokeTable[i].Modifiers = 0;
        _keystrokeTable[i].Function = FUNCTION_INPUT;
    }
    for (int i = 0; i < 26; i++)
    {
        _keystrokeTable[i+10].VirtualKey = 'A' + i;
        _keystrokeTable[i+10].Modifiers = 0;
        _keystrokeTable[i+10].Function = FUNCTION_INPUT;
    }
	/*
	#define VK_OEM_1          0xBA   // ';:' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_COMMA      0xBC   // ',' any country
	#define VK_OEM_MINUS      0xBD   // '-' any country
	#define VK_OEM_PERIOD     0xBE   // '.' any country
	#define VK_OEM_2          0xBF   // '/?' for US
	#define VK_OEM_3          0xC0   // '`/~' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	*/

	_keystrokeTable[36].VirtualKey = VK_OEM_1 ;  // ';'
    _keystrokeTable[36].Modifiers = 0;
    _keystrokeTable[36].Function = FUNCTION_INPUT;
	_keystrokeTable[37].VirtualKey = VK_OEM_COMMA ;  //','
    _keystrokeTable[37].Modifiers = 0;
    _keystrokeTable[37].Function = FUNCTION_INPUT;
	_keystrokeTable[38].VirtualKey = VK_OEM_PERIOD ; //'.'
    _keystrokeTable[38].Modifiers = 0;
    _keystrokeTable[38].Function = FUNCTION_INPUT;
	_keystrokeTable[39].VirtualKey = VK_OEM_2 ; // '/'
	_keystrokeTable[39].Modifiers = 0;
    _keystrokeTable[39].Function = FUNCTION_INPUT;
	_keystrokeTable[40].VirtualKey = VK_OEM_PLUS ; // '/'
	_keystrokeTable[40].Modifiers = 0;
    _keystrokeTable[40].Function = FUNCTION_INPUT;
}



void CCompositionProcessorEngine::SetInitialCandidateListRange()
{
    for (DWORD i = 1; i <= 10; i++)
    {
        DWORD* pNewIndexRange = nullptr;

        pNewIndexRange = _candidateListIndexRange.Append();
        if (pNewIndexRange != nullptr)
        {
            if (i != 10)
            {
                *pNewIndexRange = i;
            }
            else
            {
                *pNewIndexRange = 0;
            }
        }
    }
}



//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {        fComposing = FALSE;
    }

    if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
        {
            return TRUE;
        }
        else if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
            (IsWildcard() && IsWildcardChar(*pwch) &&  IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength()))
        {
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = FUNCTION_INPUT;
            }
            return TRUE;
        }
        else if (_hasWildcardIncludedInKeystrokeBuffer && uCode == VK_SPACE)
        {
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT_WILDCARD; } return TRUE;
        }
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }

        if (hasCandidateWithWildcard)
        {
            if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidateWildcard))
            {
                return isRetCode;
            }
        }

        // Candidate list could not handle key. We can try to restart the composition.
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            if (candidateMode != CANDIDATE_ORIGINAL)
            {
                return TRUE;
            }
            else
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT; } 
                return TRUE;
            }
        }
    } 

    // CANDIDATE_INCREMENTAL should process Keystroke.Candidate virtual keys.
    else if (candidateMode == CANDIDATE_INCREMENTAL)
    {
        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }
    }

    if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION) 
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            return TRUE;
        }
    }

    // System pre-defined keystroke
    if (fComposing)
    {
        if ((candidateMode != CANDIDATE_INCREMENTAL))
        {
            switch (uCode)
            {
            case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
            case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT;//FUNCTION_FINALIZE_CANDIDATELIST;
							} return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
            case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
            case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
            case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

            case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
            case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

            case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
            }
        }
        else if (candidateMode == CANDIDATE_INCREMENTAL)
        {
            switch (uCode)
            {
                // VK_LEFT, VK_RIGHT - set *pIsEaten = FALSE for application could move caret left or right.
                // and for CUAS, invoke _HandleCompositionCancel() edit session due to ignore CUAS default key handler for send out terminate composition
            case VK_LEFT:
            case VK_RIGHT:
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                }
                return FALSE;

            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT;} return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

                // VK_BACK - remove one char from reading string.
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
            case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
            case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
            case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

            case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
            case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

            case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
            }
        }
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        switch (uCode)
        {
        case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
        case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
        case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
        case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;
        case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
        case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
		case VK_LEFT:
        case VK_RIGHT:
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                }
                return FALSE;
        case VK_RETURN: 
        case VK_SPACE:  if (pKeyState) 
						{ 
							if ( (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)){ // space finalized the associate here instead of choose the first one (selected index = -1 for phrase candidates).
								if (pKeyState)
								{
									pKeyState->Category = CATEGORY_CANDIDATE;
									pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST;
								}
							}else{
								pKeyState->Category = CATEGORY_CANDIDATE; 
								pKeyState->Function = FUNCTION_CONVERT; 
							}

							return TRUE;
						}
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

        case VK_ESCAPE:
            {
                if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
                    }
                    return TRUE;
                }
                else
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_CANDIDATE;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                    return TRUE;
                }
            }
        }

        if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            if (IsVirtualKeyKeystrokeComposition(uCode, NULL, FUNCTION_NONE))
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT; } return TRUE;
            }
        }
    }

    if (IsKeystrokeRange(uCode, pKeyState, candidateMode))
    {
        return TRUE;
    }
    else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
    {
        return FALSE;
    }

    if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
            pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
        }
        return FALSE;
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    for (UINT i = 0; i < _KeystrokeComposition.Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = _KeystrokeComposition.GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            if (function == FUNCTION_NONE)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
				return TRUE;
            }
            else if (function == pKeystroke->Function)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
				return TRUE;
            }
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CTSFTTSArray<_KEYSTROKE> *pKeystrokeMetric)
{
	candidateMode;
    if (pfRetCode == nullptr)
    {
        return FALSE;
    }
    *pfRetCode = FALSE;

    for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = pKeystrokeMetric->GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            *pfRetCode = TRUE;
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_CANDIDATE;
					//(candidateMode == CANDIDATE_ORIGINAL ? CATEGORY_CANDIDATE :
                    //candidateMode == CANDIDATE_PHRASE ? CATEGORY_PHRASE : CATEGORY_CANDIDATE);

                pKeyState->Function = pKeystroke->Function;
            }
            return TRUE;
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    if (_candidateListIndexRange.IsRange(uCode, candidateMode))
    {
        if (candidateMode == CANDIDATE_PHRASE)
        {
            // Candidate phrase could specify modifier
             if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT) )!= 0) || //shift + 123...
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
				pKeyState->Category = CATEGORY_CANDIDATE;//CATEGORY_PHRASE; 
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            else
            {
                pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT;
                return FALSE;
            }
        }
        else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            // Candidate phrase could specify modifier
            if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT) )!= 0) || //shift + 123...
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            // else next composition
        }
        else if (candidateMode != CANDIDATE_NONE)
        {
            pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
            return TRUE;
        }
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::doBeep
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::DoBeep()
{
	if(_pTextService->GetDoBeep())
		MessageBeep(MB_ICONASTERISK);
}
