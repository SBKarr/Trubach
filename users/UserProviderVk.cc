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

class OAuthProvider : public Provider {
public:
	OAuthProvider(const StringView &str, const data::Value &data) : Provider(str, data) { }

	virtual uint64_t onAccessToken(Request &rctx, const StringView &token, const StringView &email, int64_t userId, int64_t expires) const = 0;

	virtual String onLogin(Request &rctx, const StringView &state, const StringView &redirectUrl) const override {
		if (_authorizeEndpoint.empty()) {
			messages::error("OAuthProvider", "invalid authorization endpoint");
			return String();
		}

		StringStream url;
		url << _authorizeEndpoint << "?client_id=" << _clientId << "";

		if (!_scope.empty()) { url << "&scope=" << _scope; }

		url << "&response_type=code&redirect_uri=" << redirectUrl << "&state=" << state;

		return url.str();
	}

	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const override {
		return String();
	}

	virtual uint64_t onResult(Request &rctx, const StringView &redirectUrl) const override {
		if (_tokenEndpoint.empty()) {
			messages::error("OAuthProvider", "invalid token endpoint");
			return 0;
		}

		auto &code = rctx.getParsedQueryArgs().getString("code");

		auto url = toString(_tokenEndpoint, "?code=", code, "&"
				"client_id=", _clientId, "&"
				"client_secret=", _clientSecret, "&"
				"redirect_uri=", redirectUrl);

		network::Handle h(network::Handle::Method::Get, url);
		auto data = h.performDataQuery();
		return onAccessToken(rctx, data.getString("access_token"), data.getString("email"), data.getInteger("user_id"), data.getInteger("expires_in"));
	}

	virtual uint64_t onToken(Request &rctx, const StringView &tokenStr) const override {
		return 0;
	}

protected:
	String _authorizeEndpoint;
	String _tokenEndpoint;
	String _scope;
};

class VkProvider : public OAuthProvider {
public:
	VkProvider(const StringView &str, const data::Value &data)
	: OAuthProvider(str, data) {
		_authorizeEndpoint = "https://oauth.vk.com/authorize";
		_tokenEndpoint = "https://oauth.vk.com/access_token";
		_scope = "+4194304";
	}

	data::Value makeVkUserPatch(const data::Value &u) const {
		data::Value patch;
		for (auto &it : u.asDict()) {
			if (it.first == "last_name") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "familyName");
				}
			} else if (it.first == "first_name") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "givenName");
				}
			} else if (it.first == "nickname") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "middleName");
				}
			} else if (it.first == "photo_100") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "picture");
				}
			}
		}

		auto &middleName = patch.getString("middleName");
		if (!middleName.empty()) {
			patch.setString(toString(patch.getString("givenName"), " ", middleName, " ", patch.getString("familyName")), "fullName");
		} else {
			patch.setString(toString(patch.getString("givenName"), " ", patch.getString("familyName")), "fullName");
		}

		return patch;
	}

	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const override {
		auto userId = StringView(data.getString("id")); userId += "vk-"_len;

		auto token = data.getValue("data").getString("code");
		auto expires = Time::microseconds(data.getValue("data").getInteger("expires"));
		if (expires >= Time::now()) {
			auto url = toString("https://api.vk.com/method/users.get?user_ids=", userId,
							"&fields=email,first_name,last_name,nickname,photo_100"
								"&access_token=", token, "&v=5.101");

			network::Handle h(network::Handle::Method::Get, url);
			auto data = h.performDataQuery();
			if (auto u = data.getValue("response").getValue(0)) {
				auto patch = makeVkUserPatch(u);

				auto sub = toString("vk-", u.getInteger("id"));
				if (auto user = authUser(rctx, sub, move(patch))) {
					return toString("/auth/external/success?user=", user.getInteger("__oid"));
				}
			}

		}
		return String();
	}

	virtual uint64_t onAccessToken(Request &rctx, const StringView &token, const StringView &email, int64_t userId, int64_t expires) const override {
		auto url = toString("https://api.vk.com/method/users.get?user_ids=", userId,
				"&fields=email,first_name,last_name,nickname,photo_100"
					"&access_token=", token, "&v=5.101");

		network::Handle h(network::Handle::Method::Get, url);
		auto data = h.performDataQuery();
		if (auto u = data.getValue("response").getValue(0)) {
			auto patch = makeVkUserPatch(u);
			if (!email.empty()) {
				patch.setString(email, "email");
				patch.setBool(true, "emailValidated");
			}
			patch.setValue(data::Value({
				pair("code", data::Value(token)),
				pair("expires", data::Value((Time::now() + TimeInterval::seconds(expires)).toMicros())),
			}), "data");

			auto sub = toString("vk-", u.getInteger("id"));
			if (auto user = authUser(rctx, sub, move(patch))) {
				return uint64_t(user.getInteger("__oid"));
			}
		}

		return 0;
	}
};

class FacebookProvider : public OAuthProvider {
public:
	FacebookProvider(const StringView &str, const data::Value &data)
	: OAuthProvider(str, data) {
		_authorizeEndpoint = "https://www.facebook.com/v4.0/dialog/oauth";
		_tokenEndpoint = "https://graph.facebook.com/v4.0/oauth/access_token";
		_scope = "email";
	}

	data::Value makeFbUserPatch(const data::Value &u) const {
		if (!u) {
			return data::Value();
		}

		data::Value patch;
		for (auto &it : u.asDict()) {
			if (it.first == "last_name") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "familyName");
				}
			} else if (it.first == "first_name") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "givenName");
				}
			} else if (it.first == "middle_name") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "middleName");
				}
			} else if (it.first == "email") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(move(it.second), "email");
					patch.setBool(true, "emailValidated");
				}
			} else if (it.first == "id") {
				if (it.second.isString() && !it.second.empty()) {
					patch.setValue(toString("fb-", it.second.getString()), "id");
				}
			} else if (it.first == "picture") {
				if (auto &d = it.second.getValue("data")) {
					auto &it = d.getString("url");
					if (!it.empty()) {
						patch.setString(move(it), "picture");
					}
				}
			}
		}

		auto &middleName = patch.getString("middleName");
		if (!middleName.empty()) {
			patch.setString(toString(patch.getString("givenName"), " ", middleName, " ", patch.getString("familyName")), "fullName");
		} else {
			patch.setString(toString(patch.getString("givenName"), " ", patch.getString("familyName")), "fullName");
		}

		return patch;
	}

	bool isTokenValid(const StringView &token, const StringView &appProof) const {
		auto url = toString("https://graph.facebook.com/debug_token?input_token=", token,
				"&access_token=", token, "&appsecret_proof=", appProof);
		network::Handle h(network::Handle::Method::Get, url);
		if (auto data = h.performDataQuery()) {
			auto &d = data.getValue("data");
			if (!d.getBool("is_valid")) {
				return false;
			}

			bool emailAllowed = false;
			for (auto &it : d.getArray("scopes")) {
				if (it.getString() == "email") {
					emailAllowed = true;
				}
			}

			if (!emailAllowed) {
				return false;
			}

			if (Time::now() >= Time::seconds(d.getInteger("expires_at"))) {
				return false;
			}

			return true;
		}
		return false;
	}

	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const override {
		auto userId = StringView(data.getString("id")); userId += "vk-"_len;

		auto token = data.getValue("data").getString("code");
		auto appProof = base16::encode(string::Sha256::hmac(token, _clientSecret));

		if (!isTokenValid(token, appProof)) {
			return String();
		}

		String url = toString("https://graph.facebook.com/v4.0/me?fields=first_name,last_name,middle_name,email,picture"
			"&access_token=", token, "&appsecret_proof=", appProof);
		network::Handle h(network::Handle::Method::Get, url);
		if (auto patch = makeFbUserPatch(h.performDataQuery())) {
			auto sub = patch.getString("id");
			if (auto user = authUser(rctx, sub, move(patch))) {
				return toString("/auth/external/success?user=", user.getInteger("__oid"));
			}
		}
		return String();
	}

	virtual uint64_t onAccessToken(Request &rctx, const StringView &token, const StringView &email, int64_t, int64_t expires) const override {
		auto appProof = base16::encode(string::Sha256::hmac(token, _clientSecret));

		if (!isTokenValid(token, appProof)) {
			return 0;
		}

		String url = toString("https://graph.facebook.com/v4.0/me?fields=first_name,last_name,middle_name,email,picture"
			"&access_token=", token, "&appsecret_proof=", appProof);
		network::Handle h(network::Handle::Method::Get, url);
		auto data = h.performDataQuery();
		if (auto patch = makeFbUserPatch(data)) {
			patch.setValue(data::Value({
				pair("code", data::Value(token)),
			}), "data");

			auto sub = patch.getString("id");
			if (auto user = authUser(rctx, sub, move(patch))) {
				return uint64_t(user.getInteger("__oid"));
			}
		}
		return 0;
	}
};

NS_SA_EXT_END(trubach)
