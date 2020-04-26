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

class LocalProvider : public Provider {
public:
	LocalProvider(const StringView &str, const data::Value &data) : Provider(str, data) { }

	virtual String onLogin(Request &rctx, const StringView &state, const StringView &redirectUrl) const override {
		return String();
	}

	virtual String onTry(Request &rctx, const data::Value &data, const StringView &state, const StringView &redirectUrl) const override {
		return String();
	}

	virtual uint64_t onResult(Request &rctx, const StringView &redirectUrl) const override {
		auto &args = rctx.getParsedQueryArgs();
		if (args.isString("id_token")) {
			auto h = rctx.server().getComponent<UsersComponent>();

			auto iss = rctx.getFullHostname();

			JsonWebToken token(args.getString("id_token"));
			if (token.validate(JsonWebToken::RS512, h->getPublicKey()) && token.validatePayload(iss, iss)) {
				auto sub = token.payload.getString("sub");
				auto patch = makeUserPatch(token);
				patch.setString(_name, "provider");

				if (auto user = authUser(rctx, sub, move(patch))) {
					return uint64_t(user.getInteger("__oid"));
				} else {
					return 0;
				}
			}
		}
		return 0;
	}

	virtual uint64_t onToken(Request &rctx, const StringView &tokenStr) const override {
		return 0;
	}

	virtual data::Value makeProviderPatch(data::Value &&val) const override {
		data::Value ret(Provider::makeProviderPatch(move(val)));

		ret.setBool(ret.getBool("isActivated"), "emailValidated");

		auto &middleName = ret.getString("middleName");
		if (!middleName.empty()) {
			ret.setString(toString(ret.getString("givenName"), " ", middleName, " ", ret.getString("familyName")), "fullName");
		} else {
			ret.setString(toString(ret.getString("givenName"), " ", ret.getString("familyName")), "fullName");
		}
		return ret;
	}

	virtual String getUserId(const data::Value &val) const override {
		return toString("local-", val.getInteger("__oid"));
	}
};

NS_SA_EXT_END(trubach)
