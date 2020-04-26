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

#ifndef SRC_USERS_USERSCOMPONENT_H_
#define SRC_USERS_USERSCOMPONENT_H_

#include "Trubach.h"
#include "UserProvider.h"
#include "ExternalSession.h"

NS_SA_EXT_BEGIN(trubach)

class UsersComponent : public ServerComponent {
public:
	static String makeStateToken();

	UsersComponent(Server &serv, const String &name, const data::Value &dict);
	virtual ~UsersComponent() { }

	void init(const TrubachComponent *);

	virtual void onChildInit(Server &) override;
	virtual void onStorageTransaction(storage::Transaction &) override;

	String onLogin(Request &rctx, const StringView &provider, const StringView &state, const StringView &redirectUrl) const;
	String onTry(Request &rctx, const StringView &provider, const data::Value &data, const StringView &state, const StringView &redirectUrl) const;
	uint64_t onToken(Request &rctx, const StringView &provider, const StringView &token);
	uint64_t onResult(Request &rctx, const StringView &provider, const StringView &redirectUrl) const;

	void setNewUserCallback(Function<void(const data::Value &)> &&);

	void sendActivationEmail(Request &rctx, const data::Value &newUser) const;

	bool isLongTermAuthorized(Request &rctx, uint64_t uid) const;
	bool isSessionAuthorized(Request &rctx, uint64_t uid) const;

	bool initLongTerm(Request &rctx, uint64_t uid) const;

	bool pushAuthUser(Request &rctx, uint64_t uid) const;

	Provider *getProvider(const StringView &) const;
	Provider *getLocalProvider() const;

	StringView getPrivateKey() const;
	StringView getPublicKey() const;
	SessionKeys getKeyPair() const;

	StringView getCaptchaSecret() const;

	StringView getBindAuth() const;
	StringView getBindLocal() const;

	int64_t isValidAppRequest(Request &) const; // returns application id
	data::Value isValidSecret(Request &, const data::Value &) const;

	const Scheme & getApplicationScheme() const;
	const Scheme & getExternalUserScheme() const;
	const Scheme & getProvidersScheme() const;
	const Scheme & getLocalUserScheme() const;
	const Scheme & getConnectionScheme() const;

	data::Value getExternalUserByEmail(const StringView &) const;
	data::Value getLocaUserByName(const StringView &) const;

	bool verifyCaptcha(const StringView &captchaToken) const;

	data::Value makeConnection(Request &rctx, int64_t userId, int64_t appId, const StringView &token);

	void onNewUser(const data::Value &);

protected:
	void updateDiscovery();

	data::Value readConfig() const;

	data::Value createUserCmd(const StringView &);
	data::Value addApplicationCmd(const StringView &);

	data::Value attachUser(int64_t local, int64_t extarnal);

	using Field = storage::Field;
	using Flags = storage::Flags;
	using RemovePolicy = storage::RemovePolicy;
	using Transform = storage::Transform;
	using Scheme = storage::Scheme;
	using MaxFileSize = storage::MaxFileSize;
	using MaxImageSize = storage::MaxImageSize;
	using Thumbnail = storage::Thumbnail;

	Scheme _externalUsers = Scheme("external_users");
	Scheme _authProviders = Scheme("auth_providers");
	Scheme _localUsers = Scheme("local_users");
	Scheme _connections = Scheme("user_connections");
	Scheme _applications = Scheme("user_applications");

	String _privateKey;
	String _publicKey;
	String _captchaSecret;

	mutable Mutex _mutex;

	data::Value _config;
	Map<String, Provider *> _providers;
	Provider *_localProvider = nullptr;

	Function<void(const data::Value &)> _newUserCallback;
};

NS_SA_EXT_END(trubach)

#endif /* SRC_USERS_USERSCOMPONENT_H_ */
