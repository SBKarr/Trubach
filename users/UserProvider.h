/*
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004

Copyright (C) 2020 SBKarr <sbkarr@stappler.org>

Everyone is permitted to copy and distribute verbatim or modified
copies of this license document, and changing it is allowed as long
as the name is changed.

           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.
*/

#ifndef SRC_USERS_USERPROVIDER_H_
#define SRC_USERS_USERPROVIDER_H_

#include "Trubach.h"
#include "JsonWebToken.h"

NS_SA_EXT_BEGIN(trubach)

class DiscoveryDocument : public SharedObject {
public:
	using KeyData = JsonWebToken::KeyData;

	virtual bool init(const data::Value &);

	const String &getIssuer() const;
	const String &getTokenEndpoint() const;
	const String &getAuthorizationEndpoint() const;

	const KeyData *getKey(const StringView &) const;

	DiscoveryDocument(mem::pool_t *p) : SharedObject(p) { }

protected:
	String issuer;
	String authorizationEndpoint;
	String tokenEndpoint;
	String jwksEndpoint;

	Map<String, KeyData> keys;
	uint64_t mtime = 0;
};

class Provider : public AllocPool {
public:
	using Scheme = storage::Scheme;
	using Transaction = storage::Transaction;

	static Provider *create(const StringView &name, const data::Value &);

	virtual ~Provider() { }

	virtual String onLogin(Request &rctx, const StringView &state, const StringView &redirectUrl) const = 0;
	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const = 0;
	virtual uint64_t onResult(Request &rctx, const StringView &redirectUrl) const = 0;
	virtual uint64_t onToken(Request &rctx, const StringView &tokenStr) const = 0;

	virtual data::Value authUser(Request &rctx, const StringView &sub, data::Value &&userPatch) const;

	virtual void update(Mutex &);

	const data::Value &getConfig() const;

	virtual String getUserId(const data::Value &) const;

	virtual data::Value initUser(int64_t currentUser, const storage::Transaction &t, data::Value &&userPatch) const;
	virtual data::Value initUser(int64_t currentUser, const storage::Transaction &t, const StringView &, data::Value &&userPatch, int *status = nullptr) const;
	virtual data::Value makeProviderPatch(data::Value &&) const;

protected:
	data::Value makeUserPatch(const JsonWebToken &token) const;

	Provider(const StringView &, const data::Value &);

	data::Value _config;
	String _name;
	String _type;
	String _clientId;
	String _clientSecret;
};

NS_SA_EXT_END(trubach)

#endif /* SRC_USERS_USERPROVIDER_H_ */
