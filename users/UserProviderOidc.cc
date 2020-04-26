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

class OidcProvider : public Provider {
public:
	OidcProvider(const StringView &str, const data::Value &data) : Provider(str, data) {
		if (data.isString("discovery")) {
			_discoveryUrl = data.getString("discovery");
		}
		if (data.isArray("external_ids")) {
			for (auto &it : data.getArray("external_ids")) {
				_externaIds.emplace_back(it.asString());
			}
		}
	}

	virtual String onLogin(Request &rctx, const StringView &state, const StringView &redirectUrl) const override {
		String endpoint;
		if (auto doc = _discovery) {
			endpoint = doc->getAuthorizationEndpoint();
		}
		if (endpoint.empty()) {
			messages::error("OauthGoogleHandler", "invalid `authorization_endpoint`");
			return String();
		}

		StringStream url;
		url << endpoint << "?client_id=" << _clientId << "&response_type=code&scope=openid%20email%20profile&"
				"redirect_uri=" << redirectUrl << "&"
				"state=" << state;

		return url.str();
	}

	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const override {
		String endpoint;
		if (auto doc = _discovery) {
			endpoint = doc->getAuthorizationEndpoint();
		}
		if (endpoint.empty()) {
			messages::error("OauthGoogleHandler", "invalid `authorization_endpoint`");
			return String();
		}

		StringStream url;
		url << endpoint << "?client_id=" << _clientId << "&response_type=code&scope=openid%20email%20profile&"
				"redirect_uri=" << redirectUrl << "&"
				"state=" << state << "&prompt=none";

		if (data && data.isString("id")) {
			url << "&login_hint=" << data.getString("id");
		}

		return url.str();
	}

	virtual uint64_t onResult(Request &rctx, const StringView &redirectUrl) const override {
		auto doc = _discovery;
		if (!doc) {
			messages::error("OauthGoogleHandler", "OpenID discovery document not loaded");
			return 0;
		}

		auto endpoint = doc->getTokenEndpoint();
		if (endpoint.empty()) {
			messages::error("OauthGoogleHandler", "`token_endpoint` not found in OpenID discovery document");
			return 0;
		}

		auto &args = rctx.getParsedQueryArgs();

		StringStream stream;
		stream << "code=" << args.getString("code") << "&"
				"client_id=" << _clientId << "&"
				"client_secret=" << _clientSecret << "&"
				"redirect_uri=" << redirectUrl << "&"
				"grant_type=authorization_code";

		network::Handle h(network::Handle::Method::Post, endpoint);
		h.addHeader("Content-Type: application/x-www-form-urlencoded");
		h.setSendData(stream.weak());

		auto data = h.performDataQuery();

		if (data && data.isString("id_token")) {
			JsonWebToken token(data.getString("id_token"));
			if (auto key = doc->getKey(token.kid)) {
				if (token.validate(*key)) {
					auto sub = token.payload.getString("sub");
					if (sub.empty()) {
						messages::error("UserDatabase", "Empty sub key", data::Value({
							pair("sub", data::Value(sub)),
							pair("url", data::Value(redirectUrl)),
						}));
						return 0;
					}

					auto patch = makeUserPatch(token);
					patch.setString(_name, "provider");

					if (auto user = authUser(rctx, sub, move(patch))) {
						return uint64_t(user.getInteger("__oid"));
					} else {
						messages::error("UserDatabase", "Fail to authorize user", data::Value({
							pair("sub", data::Value(sub)),
							pair("patch", data::Value(patch)),
							pair("url", data::Value(redirectUrl)),
						}));
						return 0;
					}
				}
			}
		}

		messages::error("UserDatabase", "Fail read auth data", data::Value({
			pair("data", data::Value(data)),
			pair("url", data::Value(redirectUrl)),
		}));
		return 0;
	}

	virtual uint64_t onToken(Request &rctx, const StringView &tokenStr) const override {
		auto doc = _discovery;
		if (!doc) {
			messages::error("OauthGoogleHandler", "OpenID discovery document not loaded");
			return 0;
		}

		if (!tokenStr.empty()) {
			JsonWebToken token(tokenStr);
			if (auto key = doc->getKey(token.kid)) {
				if (token.validate(*key)) {
					bool valid = false;
					if (token.validatePayload(doc->getIssuer(), _clientId)) {
						valid = true;
					} else {
						for (auto &it : _externaIds) {
							if (token.validatePayload(doc->getIssuer(), it)) {
								valid = true;
								break;
							}
						}
					}

					if (!valid) {
						messages::error("OauthGoogleHandler", "Fail to validate token payload");
						return 0;
					}

					auto sub = token.payload.getString("sub");
					if (sub.empty()) {
						return HTTP_INTERNAL_SERVER_ERROR;
					}

					auto patch = makeUserPatch(token);
					if (auto user = authUser(rctx, sub, move(patch))) {
						return uint64_t(user.getInteger("__oid"));
					} else {
						return 0;
					}
				} else {
					messages::error("OauthGoogleHandler", "Fail to validate token signing");
				}
			} else {
				messages::error("OauthGoogleHandler", "No key for kid", data::Value(token.kid));
			}
		} else {
			messages::error("OauthGoogleHandler", "Invalid token");
		}
		return 0;
	}

	virtual void update(Mutex &mutex) override {
		network::Handle h(network::Handle::Method::Get, _discoveryUrl);
		auto discoveryDoc = h.performDataQuery();

		auto ptr = DiscoveryDocument::create<DiscoveryDocument>(Server(mem::server()).getPool(), [&] (DiscoveryDocument &doc) {
			doc.init(discoveryDoc);
		});

		std::unique_lock<Mutex> lock(mutex);
		_discovery = ptr;
	}

protected:
	String _discoveryUrl;
	Vector<String> _externaIds;
	Rc<DiscoveryDocument> _discovery;
};

NS_SA_EXT_END(trubach)
