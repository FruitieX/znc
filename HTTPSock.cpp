/*
 * Copyright (C) 2004-2008  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifdef _MODULES

#include "HTTPSock.h"
#include "znc.h"

CHTTPSock::CHTTPSock() : Csock() {
	m_bSentHeader = false;
	m_bGotHeader = false;
	m_bLoggedIn = false;
	m_bPost = false;
	m_bDone = false;
	m_uPostLen = 0;
	EnableReadLine();
}

CHTTPSock::CHTTPSock(const CString& sHostname, unsigned short uPort, int iTimeout) : Csock(sHostname, uPort, iTimeout) {
	m_bSentHeader = false;
	m_bGotHeader = false;
	m_bLoggedIn = false;
	m_bPost = false;
	m_bDone = false;
	m_uPostLen = 0;
	EnableReadLine();
}

CHTTPSock::~CHTTPSock() {}

void CHTTPSock::ReadData(const char* data, int len) {
	string s;
	s.append(data, len);

	if (!m_bDone && m_bGotHeader && m_bPost) {
		m_sPostData.append(data, len);
		CheckPost();
	}
}

void CHTTPSock::CheckPost() {
	if (m_sPostData.size() >= m_uPostLen) {
		ParseParams(m_sPostData.Left(m_uPostLen));
		GetPage();
		m_sPostData.clear();
		m_bDone = true;
		Close(Csock::CLT_AFTERWRITE);
	}
}

void CHTTPSock::ReadLine(const CString& sData) {
	CString sLine = sData;
	sLine.TrimRight("\r\n");

	if (m_bGotHeader) {
		return;
	}

	CString sName = sLine.Token(0);

	if (sName.CaseCmp("GET") == 0) {
		m_bPost = false;
		m_sURI = sLine.Token(1);
		ParseURI();
	} else if (sName.CaseCmp("POST") == 0) {
		m_bPost = true;
		m_sURI = sLine.Token(1);
		ParseURI();
	} else if (sName.CaseCmp("Authorization:") == 0) {
		CString sUnhashed;
		sLine.Token(2).Base64Decode(sUnhashed);
		m_sUser = sUnhashed.Token(0, false, ":");
		m_sPass = sUnhashed.Token(1, true, ":");
		m_bLoggedIn = OnLogin(m_sUser, m_sPass);
	} else if (sName.CaseCmp("Content-Length:") == 0) {
		m_uPostLen = sLine.Token(1).ToULong();
	} else if (sLine.empty()) {
		m_bGotHeader = true;
		DisableReadLine();

		if (m_bPost) {
			m_sPostData = GetInternalReadBuffer();
			CheckPost();
		} else {
			GetPage();
		}
	}
}

void CHTTPSock::GetPage() {
	CString sPage;

	DEBUG_ONLY(cout << "Page Request [" << m_sURI << "] ");

	if (!OnPageRequest(m_sURI, sPage)) {
		PrintNotFound();
		return;
	}

	if (PrintHeader(sPage.length())) {
		Write(sPage);
		Close(Csock::CLT_AFTERWRITE);
	}
}

bool CHTTPSock::PrintFile(const CString& sFileName, CString sContentType) {
	CString sFilePath = sFileName;

	if (!m_sDocRoot.empty()) {
		while (sFilePath.Left(1) == "/") {
			sFilePath.LeftChomp(1);
		}

		sFilePath = CDir::ChangeDir(m_sDocRoot, sFilePath, m_sDocRoot);

		if (sFilePath.Left(m_sDocRoot.size()) != m_sDocRoot) {
			PrintErrorPage(403, "Forbidden", "You don't have permission to access that file on this server.");
			DEBUG_ONLY(cout << "THIS FILE:     [" << sFilePath << "] does not live in ..." << endl);
			DEBUG_ONLY(cout << "DOCUMENT ROOT: [" << m_sDocRoot << "]" << endl);
			return false;
		}
	}

	CFile File(sFilePath);

	if (!File.Open(CFile::F_Read)) {
		PrintNotFound();
		return false;
	}

	if (sContentType.empty()) {
		if (sFileName.Right(5).CaseCmp(".html") == 0 || sFileName.Right(4).CaseCmp(".htm") == 0) {
			sContentType = "text/html";
		} else if (sFileName.Right(4).CaseCmp(".css") == 0) {
			sContentType = "text/css";
		} else if (sFileName.Right(3).CaseCmp(".js") == 0) {
			sContentType = "application/x-javascript";
		} else if (sFileName.Right(4).CaseCmp(".jpg") == 0) {
			sContentType = "image/jpeg";
		} else if (sFileName.Right(4).CaseCmp(".gif") == 0) {
			sContentType = "image/gif";
		} else if (sFileName.Right(4).CaseCmp(".ico") == 0) {
			sContentType = "image/x-icon";
		} else if (sFileName.Right(4).CaseCmp(".png") == 0) {
			sContentType = "image/png";
		} else if (sFileName.Right(4).CaseCmp(".bmp") == 0) {
			sContentType = "image/bmp";
		} else {
			sContentType = "text/plain";
		}
	}

	PrintHeader(File.GetSize(), sContentType);

	char szBuf[4096];
	int iLen = 0;

	while ((iLen = File.Read(szBuf, 4096)) > 0) {
		Write(szBuf, iLen);
	}

	Close(Csock::CLT_AFTERWRITE);

	return true;
}

void CHTTPSock::ParseURI() {
	ParseParams(m_sURI.Token(1, true, "?"));
	m_sURI = m_sURI.Token(0, false, "?");
}

void CHTTPSock::ParseParams(const CString& sParams) {
	m_msvsParams.clear();

	VCString vsPairs;
	sParams.Split("&", vsPairs, true);

	for (unsigned int a = 0; a < vsPairs.size(); a++) {
		const CString& sPair = vsPairs[a];
		CString sName = sPair.Token(0, false, "=").Escape_n(CString::EURL, CString::EASCII);
		CString sValue = sPair.Token(1, true, "=").Escape_n(CString::EURL, CString::EASCII);

		m_msvsParams[sName].push_back(sValue);
	}
}

void CHTTPSock::SetDocRoot(const CString& s) {
	m_sDocRoot = s + "/";
	m_sDocRoot.Replace("//", "/");
}

const CString& CHTTPSock::GetDocRoot() const {
	return m_sDocRoot;
}

const CString& CHTTPSock::GetUser() const {
	return m_sUser;
}

const CString& CHTTPSock::GetPass() const {
	return m_sPass;
}

const CString& CHTTPSock::GetContentType() const {
	return m_sContentType;
}

const CString& CHTTPSock::GetParamString() const {
	return m_sPostData;
}

bool CHTTPSock::HasParam(const CString& sName) const {
	return (m_msvsParams.find(sName) != m_msvsParams.end());
}

CString CHTTPSock::GetParam(const CString& sName) const {
	CString sRet;

	VCString vsParams;
	if (GetParamValues(sName, vsParams)) {
		sRet = vsParams[0];
	}

	return sRet;
}

unsigned int CHTTPSock::GetParamValues(const CString& sName, set<CString>& ssRet) const {
	ssRet.clear();

	map<CString, VCString>::const_iterator it = m_msvsParams.find(sName);

	if (it != m_msvsParams.end()) {
		for (unsigned int a = 0; a < it->second.size(); a++) {
			ssRet.insert(it->second[a]);
		}
	}

	return ssRet.size();
}

unsigned int CHTTPSock::GetParamValues(const CString& sName, VCString& vsRet) const {
	vsRet.clear();

	map<CString, VCString>::const_iterator it = m_msvsParams.find(sName);

	if (it != m_msvsParams.end()) {
		vsRet = it->second;
	}

	return vsRet.size();
}

const map<CString, VCString>& CHTTPSock::GetParams() const {
	return m_msvsParams;
}

bool CHTTPSock::OnPageRequest(const CString& sURI, CString& sPageRet) {
	return false;
}

bool CHTTPSock::PrintNotFound() {
	return PrintErrorPage(404, "Not Found", "The requested URL was not found on this server.");
}

bool CHTTPSock::PrintErrorPage(unsigned int uStatusId, const CString& sStatusMsg, const CString& sMessage) {
	if (SentHeader()) {
		return false;
	}

	CString sPage = GetErrorPage(uStatusId, sStatusMsg, sMessage);
	PrintHeader(sPage.length(), "text/html", uStatusId, sStatusMsg);
	Write(sPage);
	Close(Csock::CLT_AFTERWRITE);

	return true;
}

CString CHTTPSock::GetErrorPage(unsigned int uStatusId, const CString& sStatusMsg, const CString& sMessage) {
	return "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n<title>" + CString(uStatusId) + " " + sStatusMsg.Escape_n(CString::EHTML) + "</title>\r\n"
		"</head><body>\r\n<h1>" + sStatusMsg.Escape_n(CString::EHTML) + "</h1>\r\n"
		"<p>" + sMessage.Escape_n(CString::EHTML) + "</p>\r\n"
		"<hr>\r\n<address>" + CZNC::GetTag().Escape_n(CString::EHTML) + " at " + GetLocalIP().Escape_n(CString::EHTML) + " Port " + CString(GetLocalPort()) + "</address>\r\n"
		"</body></html>\r\n";
}

bool CHTTPSock::ForceLogin() {
	if (m_bLoggedIn) {
		return true;
	}

	if (SentHeader()) {
		return false;
	}

	CString sPage = GetErrorPage(401, "Unauthorized", "You need to login to view this page.");
	AddHeader("WWW-Authenticate", "Basic realm=\"" + CZNC::GetTag() + "\"");
	PrintHeader(sPage.length(), "text/html", 401, "Unauthorized");
	Write(sPage);
	Close(Csock::CLT_AFTERWRITE);

	return false;
}

bool CHTTPSock::OnLogin(const CString& sUser, const CString& sPass) {
	return false;
}

bool CHTTPSock::SentHeader() const {
	return m_bSentHeader;
}

bool CHTTPSock::PrintHeader(unsigned long uContentLength, const CString& sContentType, unsigned int uStatusId, const CString& sStatusMsg) {
	if (SentHeader()) {
		DEBUG_ONLY(cout << "- Header already sent!" << endl);
		return false;
	}

	if (!sContentType.empty()) {
		m_sContentType = sContentType;
	}

	if (m_sContentType.empty()) {
		m_sContentType = "text/html";
	}

	DEBUG_ONLY(cout << "- " << uStatusId << " (" << sStatusMsg << ") [" << m_sContentType << "]" << endl);

	Write("HTTP/1.0 " + CString(uStatusId) + " " + sStatusMsg + "\r\n");
	//Write("Date: Tue, 28 Jun 2005 20:45:36 GMT\r\n");
	Write("Server: " + CZNC::GetTag() + "\r\n");
	Write("Content-Length: " + CString(uContentLength) + "\r\n");
	Write("Connection: Close\r\n");
	Write("Content-Type: " + m_sContentType + "\r\n");

	for (MCString::iterator it = m_msHeaders.begin(); it != m_msHeaders.end(); it++) {
		Write(it->first + ": " + it->second + "\r\n");
	}

	Write("\r\n");
	m_bSentHeader = true;

	return true;
}

void CHTTPSock::SetContentType(const CString& sContentType) {
	m_sContentType = sContentType;
}

void CHTTPSock::AddHeader(const CString& sName, const CString& sValue) {
	m_msHeaders[sName] = sValue;
}

bool CHTTPSock::Redirect(const CString& sURL) {
	if (SentHeader()) {
		return false;
	}

	DEBUG_ONLY(cout << "- Redirect to [" << sURL << "]" << endl);
	CString sPage = GetErrorPage(302, "Found", "The document has moved <a href=\"" + sURL.Escape_n(CString::EHTML) + "\">here</a>.");
	AddHeader("Location", sURL);
	PrintHeader(sPage.length(), "text/html", 302, "Found");
	Write(sPage);
	Close(Csock::CLT_AFTERWRITE);

	return true;
}

void CHTTPSock::Timeout() {
}

void CHTTPSock::SockError(int iErrno) {
}

void CHTTPSock::Connected() {
	SetTimeout(120);
}

void CHTTPSock::Disconnected() {
}

Csock* CHTTPSock::GetSockObj(const CString& sHost, unsigned short uPort) {
	CHTTPSock* pSock = new CHTTPSock;
	pSock->SetSockName("HTTP::CLIENT");
	pSock->SetTimeout(120);

	return pSock;
}

#endif // _MODULES
