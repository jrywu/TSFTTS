//#define DEBUG_PRINT

#include "TSFTTS.h"
#include "BaseStructure.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"


//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{

	debugPrint(L"CTSFTTS::_HandleCandidateWorker() \n");
    HRESULT hr = S_OK;
	CStringRange commitString;
	CTSFTTSArray<CCandidateListItem> candidatePhraseList;	
	CStringRange candidateString;
	
    if (nullptr == _pTSFTTSUIPresenter)
    {
        goto Exit; //should not happen
    }
	
	const WCHAR* pCandidateString = nullptr;
	DWORD_PTR candidateLen = 0;    

	if (!_IsComposing())
		_StartComposition(pContext);

	candidateLen = _pTSFTTSUIPresenter->_GetSelectedCandidateString(&pCandidateString);
	if (candidateLen == 0)
    {
		if(_candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION || _candidateMode == CANDIDATE_PHRASE)
		{
			_HandleCancel(ec, pContext);
			goto Exit;
		}
		else
		{
			hr = S_FALSE;
			_pCompositionProcessorEngine->DoBeep(); //beep for no valid mapping found
			goto Exit;
		}
    }
	
	
	commitString.Set(pCandidateString , candidateLen );
	
	PWCHAR pwch = new (std::nothrow) WCHAR[2];  // pCandidateString will be destroyed after _detelteCanddiateList was called.
	pwch[1] = L'0';
	if(candidateLen > 1)
	{	
		StringCchCopyN(pwch, 2, pCandidateString + candidateLen -1, 1); 
	}else // cnadidateLen ==1
	{
		StringCchCopyN(pwch, 2, pCandidateString, 1); 	
	}
	candidateString.Set(pwch, 1 );

	hr = _AddComposingAndChar(ec, pContext, &commitString);
	if (FAILED(hr))	return hr;
	
	_HandleComplete(ec, pContext);
	//_TerminateComposition(ec, pContext);
	
	

	BOOL fMakePhraseFromText = _pCompositionProcessorEngine->IsMakePhraseFromText();
	if (fMakePhraseFromText)
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(candidateString, &candidatePhraseList);
		LCID locale = GetLocale();

		_pTSFTTSUIPresenter->RemoveSpecificCandidateFromList(locale, candidatePhraseList, candidateString);
	}
	
	// We have a candidate list if candidatePhraseList.Cnt is not 0
	// If we are showing reverse conversion, use UIPresenter
	
	if (candidatePhraseList.Count())
	{
		CStringRange emptyComposition;
		if (!_IsComposing())
			_StartComposition(pContext);  //StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
		_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 
		
	
		if(SUCCEEDED(_CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext)))
		{	
			_pTSFTTSUIPresenter->_ClearCandidateList();
			_pTSFTTSUIPresenter->_SetCandidateTextColor(RGB(0, 0x80, 0), GetSysColor(COLOR_WINDOW));    // Text color is green
			_pTSFTTSUIPresenter->_SetCandidateFillColor((HBRUSH)(COLOR_WINDOW+1));    // Background color is window
			_pTSFTTSUIPresenter->_SetCandidateText(&candidatePhraseList, TRUE);
			_pTSFTTSUIPresenter->_SetCandidateSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates
			_phraseCandShowing = TRUE;  //_phraseCandShowing = TRUE. phrase cand is showing
			_candidateMode = CANDIDATE_PHRASE;
			_isCandidateWithWildcard = FALSE;	
		}
	}
	else
	{
		if(_pTSFTTSUIPresenter)
			_pTSFTTSUIPresenter->_EndCandidateList();
	}
		
	
	
	
Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFTTSUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFTTSUIPresenter)
    {
        if (_pTSFTTSUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
        {
            return _HandleCandidateConvert(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DWORD phraseLen = 0;
    const WCHAR* pPhraseString = nullptr;

    phraseLen = (DWORD)_pTSFTTSUIPresenter->_GetSelectedCandidateString(&pPhraseString);

    CStringRange phraseString, clearString;
    phraseString.Set(pPhraseString, phraseLen);

    if (phraseLen)
    {

        if ((hr = _AddComposingAndChar(ec, pContext, &phraseString)) != S_OK)
        {
            return hr;
        }
    }

    _HandleComplete(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFTTSUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFTTSUIPresenter)
    {
        if (_pTSFTTSUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
        {
            return _HandlePhraseFinalize(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _CreateAndStartCandidate
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_CreateAndStartCandidate(), _candidateMode = %d", _candidateMode);
    HRESULT hr = S_OK;

	
	if (_pTSFTTSUIPresenter == nullptr)
    {
        _pTSFTTSUIPresenter = new (std::nothrow) UIPresenter(this, pCompositionProcessorEngine);
        if (!_pTSFTTSUIPresenter)
        {
            return E_OUTOFMEMORY;
        }
	}
	
	if ((_candidateMode == CANDIDATE_NONE) && (_pTSFTTSUIPresenter))
    {
 
		// we don't cache the document manager object. So get it from pContext.
		ITfDocumentMgr* pDocumentMgr = nullptr;
		if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
		{
			// get the composition range.
			ITfRange* pRange = nullptr;
			if (SUCCEEDED(_pComposition->GetRange(&pRange)))
			{
				hr = _pTSFTTSUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
				pRange->Release();
			}
			pDocumentMgr->Release();
		}
	}
	return hr;
}


//+---------------------------------------------------------------------------
//
// _DeleteCandidateList
//
//----------------------------------------------------------------------------

VOID CTSFTTS::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
	isForce;pContext;
	debugPrint(L"CTSFTTS::_DeleteCandidateList()\n");
	if(_pCompositionProcessorEngine) 
	{
	    _pCompositionProcessorEngine->PurgeVirtualKey();
	}

    if (_pTSFTTSUIPresenter)
    {
        _pTSFTTSUIPresenter->_EndCandidateList();
        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
}
