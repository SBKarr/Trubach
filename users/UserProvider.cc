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

#include "UserProvider.h"
#include "Networking.h"
#include "UsersComponent.h"

NS_SA_EXT_BEGIN(trubach)

bool DiscoveryDocument::init(const data::Value &discoveryDoc) {
	if (discoveryDoc.isDictionary()) {
		issuer = discoveryDoc.getString("issuer");
		authorizationEndpoint = discoveryDoc.getString("authorization_endpoint");
		tokenEndpoint = discoveryDoc.getString("token_endpoint");
		jwksEndpoint = discoveryDoc.getString("jwks_uri");

		network::Handle h(network::Handle::Method::Get, jwksEndpoint);
		auto jwsDoc = h.performDataQuery();
		auto &keysArr = jwsDoc.getValue("keys");
		if (keysArr.isArray()) {
			for (auto &it : keysArr.asArray()) {
				JsonWebToken::SigAlg alg = JsonWebToken::SigAlg::RS512;
				String kid;
				Bytes n, e;
				for (auto &k_it : it.asDict()) {
					if (k_it.first == "alg") {
						alg = JsonWebToken::getAlg(k_it.second.getString());
					} else if (k_it.first == "n") {
						n = base64::decode(k_it.second.asString());
					} else if (k_it.first == "e") {
						e = base64::decode(k_it.second.asString());
					} else if (k_it.first == "kid") {
						kid = k_it.second.asString();
					}
				}

				if (!kid.empty()) {
					KeyData key(alg, n, e);
					keys.emplace(move(kid), move(key));
				}
			}
		}

		return true;
	}

	return false;
}

const String &DiscoveryDocument::getIssuer() const {
	return issuer;
}
const String &DiscoveryDocument::getTokenEndpoint() const {
	return tokenEndpoint;
}
const String &DiscoveryDocument::getAuthorizationEndpoint() const {
	return authorizationEndpoint;
}

const DiscoveryDocument::KeyData *DiscoveryDocument::getKey(const StringView &k) const {
	auto it = keys.find(k);
	if (it != keys.end()) {
		return &it->second;
	}
	return nullptr;
}

Provider *Provider::create(const StringView &name, const data::Value &data) {
	auto &type = data.getString("type");
	if (type == "oidc") {
		return new OidcProvider(name, data);
	} else if (type == "vk") {
		return new VkProvider(name, data);
	} else if (type == "facebook") {
		return new FacebookProvider(name, data);
	} else if (type == "local") {
		return new LocalProvider(name, data);
	}
	return nullptr;
}

void Provider::update(Mutex &) { }

const data::Value &Provider::getConfig() const {
	return _config;
}

data::Value Provider::makeUserPatch(const JsonWebToken &token) const {
	data::Value patch;
	for (auto &it : token.payload.asDict()) {
		if (it.first == "email") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), it.first);
			}
		} else if (it.first == "family_name" || it.first == "familyName") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), "familyName");
			}
		} else if (it.first == "given_name" || it.first == "givenName") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), "givenName");
			}
		} else if (it.first == "name" || it.first == "fullName") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), "fullName");
			}
		} else if (it.first == "picture") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), "picture");
			}
		} else if (it.first == "locale") {
			if (it.second.isString()) {
				patch.setValue(move(it.second), "locale");
			}
		}
	}
	return patch;
}

Provider::Provider(const StringView &name, const data::Value &data) : _name(name.str()) {
	for (auto &it : data.asDict()) {
		if (it.first == "id") {
			_clientId = it.second.getString();
		} else if (it.first == "secret") {
			_clientSecret = it.second.getString();
		} else if (it.first == "type") {
			_type = it.second.getString();
		}
	}

	_config = data;
}

data::Value Provider::authUser(Request &rctx, const StringView &sub, data::Value &&userPatch) const {
	auto storage = storage::Transaction::acquire(rctx.storage());
	auto serv = rctx.server();
	auto c = serv.getComponent<UsersComponent>();
	auto providerScheme = &c->getProvidersScheme();
	auto userScheme = &c->getExternalUserScheme();

	if (!storage || !providerScheme || !userScheme) {
		rctx.setStatus(HTTP_INTERNAL_SERVER_ERROR);
		return data::Value();
	}

	int status = 0;
	int64_t currentUser = 0;
	if (auto session = ExternalSession::get()) {
		currentUser = session->getUser();
	}

	if (auto ret = initUser(currentUser, storage, sub, move(userPatch), &status)) {
		return ret;
	} else {
		if (status) {
			rctx.setStatus(status);
		}
		return data::Value();
	}
}

String Provider::getUserId(const data::Value &data) const {
	return toString(data.getInteger("__oid"));
}

data::Value Provider::initUser(int64_t currentUser, const storage::Transaction &t, data::Value &&userPatch) const {
	return initUser(currentUser, t, getUserId(userPatch), std::move(userPatch));
}

data::Value Provider::initUser(int64_t currentUser, const storage::Transaction &t, const StringView &sub, data::Value &&val, int *status) const {
	auto serv = Server(mem::server());
	auto c = serv.getComponent<UsersComponent>();
	auto &providers = c->getProvidersScheme();
	auto &externals = c->getExternalUserScheme();

	data::Value ret;
	auto success = t.performAsSystem([&] {
		auto userPatch = makeProviderPatch(move(val));
		auto objs = providers.select(t, storage::Query().select("id", sub.str()).select("provider", _name));
		if (objs.size() > 1) {
			return false;
		} else if (objs.size() == 1) {
			auto &obj = objs.getValue(0);
			obj = storage::Worker(providers, t).asSystem().update(obj, userPatch);

			if (auto user = providers.getProperty(t, obj, "user")) {
				userPatch.setInteger(obj.getInteger("__oid"), "lastProvider");
				ret = externals.update(t, user, userPatch);
				return true;
			}

			if (status) { *status = HTTP_FORBIDDEN; }
		} else {
			// register user
			auto emailUsers = externals.select(t, storage::Query().select("email", userPatch.getString("email")));
			if (emailUsers.isArray() && emailUsers.size() > 0) {
				if (emailUsers.size() == 1) {
					auto user = emailUsers.getValue(0);
					if (currentUser == emailUsers.getValue(0).getInteger("__oid")) {
						userPatch.setInteger(user.getInteger("__oid"), "user");
						userPatch.setString(sub, "id");
						if (auto prov = providers.create(t, userPatch)) { // bind user account with new provider
							user = storage::Worker(externals, t).update(user, data::Value({
								pair("lastProvider", data::Value(prov.getInteger("__oid")))
							}));
						}
						ret = move(user);
						return true;
					} else {
						if (status) { *status = HTTP_FORBIDDEN; }
					}
				}
			} else {
				// create new account
				if (data::Value user = externals.create(t, userPatch)) {
					userPatch.setInteger(user.getInteger("__oid"), "user");
					userPatch.setString(sub, "id");
					if (auto prov = providers.create(t, userPatch)) {
						storage::Worker(externals, t).asSystem().update(user, data::Value({
							pair("lastProvider", data::Value(prov.getInteger("__oid")))
						}));
						c->onNewUser(user);
						ret = move(user);
						return true;
					}
				}
			}
		}
		return false;
	});

	if (success) {
		return ret;
	}

	if (status && *status != HTTP_FORBIDDEN) { *status = HTTP_INTERNAL_SERVER_ERROR; }
	return data::Value();
}

data::Value Provider::makeProviderPatch(data::Value &&val) const {
	data::Value ret(move(val));
	ret.setString(_name, "provider");
	return ret;
}

NS_SA_EXT_END(trubach)
