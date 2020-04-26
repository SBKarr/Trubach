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

#include "UsersComponent.h"
#include "Networking.h"

NS_SA_EXT_BEGIN(trubach)

class LocalAuthHandler : public DataHandler {
public:
	virtual bool isRequestPermitted(Request & rctx) override;
	virtual bool processDataHandler(Request &, data::Value &result, data::Value &input) override;
	virtual int onTranslateName(Request &rctx) override;

protected:
	User * tryUserLogin(Request &rctx, const StringView &, const StringView &);

	virtual bool processLogin(Request &rctx, data::Value &result);
	virtual bool processRegister(Request &rctx, data::Value &result);
	virtual bool processActivate(Request &rctx, data::Value &result);
	virtual bool processUpdate(Request &rctx, data::Value &input);

	virtual int onLoginInterface(Request &rctx);
	virtual int onProfileInterface(Request &rctx);
	virtual int onActivateInterface(Request &rctx);

	virtual int onPushLogin(Request &rctx, const StringView &r);

	bool verifyCaptcha(const StringView &token);

	data::Value getLocalUser(int64_t externalId);

	const UsersComponent *_component = nullptr;
	storage::Transaction _transaction = nullptr;
};

static constexpr auto CAPTCHA_URL = "https://www.google.com/recaptcha/api/siteverify";

bool LocalAuthHandler::isRequestPermitted(Request & rctx) {
	_transaction = storage::Transaction::acquire(rctx.storage());
	_component = rctx.server().getComponent<UsersComponent>();
	return _component && _transaction;
}

bool LocalAuthHandler::processDataHandler(Request & rctx, data::Value &result, data::Value &input) {
	if (_subPath == "/do_login") {
		return processLogin(rctx, result);
	} else if (_subPath == "/do_register") {
		return processRegister(rctx, result);
	} else if (_subPath == "/do_activate") {
		return processActivate(rctx, result);
	} else if (_subPath == "/login") {
		auto &target = rctx.getParsedQueryArgs().getValue("target");
		auto &name = input.getString("name");
		auto &passwd = input.getString("passwd");

		if (target.isString() && StringView(target.getString()).starts_with(StringView(rctx.getFullHostname()))) {
			if (auto u = tryUserLogin(rctx, name, passwd)) {
				if (auto userId = u->getInteger("external_id")) {
					if (_component->pushAuthUser(rctx, userId)) {
						rctx.setStatus(rctx.redirectTo(String(target.getString())));
						return true;
					}
				}
			}
			if (auto session = ExternalSession::acquire(rctx)) {
				session->setValue("invalid_user", "auth_error");
			}
			rctx.setStatus(rctx.redirectTo(String(target.getString())));
		}
	} else if (_subPath == "/update") {
		auto &target = rctx.getParsedQueryArgs().getValue("target");
		if (target.isString() && StringView(target.getString()).starts_with(StringView(rctx.getFullHostname()))) {
			rctx.setStatus(rctx.redirectTo(String(target.getString())));
			return processUpdate(rctx, input);
		}
	}
	return false;
}

int LocalAuthHandler::onTranslateName(Request &rctx) {
	if (_subPath == "/login") {
		auto &target = rctx.getParsedQueryArgs().getValue("target");
		if (target.isString()) {
			_maxRequestSize = 1_KiB;
			_maxVarSize = 256;
			return DataHandler::onTranslateName(rctx);
		} else {
			return onLoginInterface(rctx);
		}
	} else if (_subPath == "/update") {
		auto &target = rctx.getParsedQueryArgs().getValue("target");
		if (target.isString()) {
			_maxRequestSize = 1_KiB;
			_maxVarSize = 256;
			return DataHandler::onTranslateName(rctx);
		} else {
			return onProfileInterface(rctx);
		}
	} else if (_subPath == "/activate") {
		return onActivateInterface(rctx);
	} else if (StringView(_subPath).starts_with("/push_login/")) {
		StringView r(_subPath); r += "/push_login/"_len;
		return onPushLogin(rctx, r);
	} else if (_subPath == "/do_login"
			|| _subPath == "/do_register"
			|| _subPath == "/do_activate"
			|| _subPath == "/do_update") {
		return DataHandler::onTranslateName(rctx);
	}

	return HTTP_NOT_FOUND;
}

User * LocalAuthHandler::tryUserLogin(Request &rctx, const StringView &name, const StringView &passwd) {
	auto userScheme = rctx.server().getScheme("local_users");
	auto storage = rctx.storage();
	if (!userScheme || !storage) {
		return nullptr;
	}

	if (name.empty() || passwd.empty()) {
		messages::error("Auth", "Name or password is not specified", data::Value({
			pair("Doc", data::Value("You should specify 'name' and 'passwd' variables in request"))
		}));
		return nullptr;
	}

	auto user = User::get(rctx.storage(), *userScheme, name, passwd);
	if (!user) {
		messages::error("Auth", "Invalid username or password");
		return nullptr;
	}

	auto userData = user->getData();
	bool isActivated = user->getBool("isActivated");
	if (isActivated) {
		if (auto prov = _component->getProvider("local")) {
			if (auto authUser = prov->authUser(rctx, toString("local-", userData.getInteger("__oid")), move(userData))) {
				if (auto id = authUser.getInteger("__oid")) {
					user->setInteger(id, "external_id");
					return user;
				}
			}
		}
	}

	messages::error("Auth", "Fail to create session");
	return user;
}

bool LocalAuthHandler::processLogin(Request &rctx, data::Value &result) {
	auto &queryData = rctx.getParsedQueryArgs();
	auto &name = queryData.getString("name");
	auto &passwd = queryData.getString("passwd");

	if (auto u = tryUserLogin(rctx, name, passwd)) {
		if (auto userId = u->getInteger("external_id")) {
			if (_component->pushAuthUser(rctx, userId)) {
				result.setInteger(userId, "user");
				result.setBool(true, "activated");
				return true;
			}
		} else if (!u->getBool("isActivated")) {
			result.setBool(false, "activated");
			return true;
		}
	}
	return false;
}

bool LocalAuthHandler::processRegister(Request &rctx, data::Value &result) {
	auto &userScheme = _component->getLocalUserScheme();
	auto &queryData = rctx.getParsedQueryArgs();

	enum FillFlags {
		None,
		Password = 1 << 0,
		Email = 1 << 1,
		GivenName = 1 << 2,
		FamilyName = 1 << 3,
		Captcha = 1 << 4,

		All = 0b11111
	};

	FillFlags flags = None;
	String captchaToken;

	data::Value newUser(data::Value::Type::DICTIONARY); newUser.asDict().reserve(6);
	newUser.setBool(false, "isActivated");

	for (auto &it : queryData.asDict()) {
		if (it.first == "email" && it.second.isString()) {
			auto email = it.second.getString();
			if (valid::validateEmail(email)) {
				newUser.setString(move(email), "email");
				flags = FillFlags(flags | Email);
			}
		} else if (it.first == "password" && it.second.isString()) {
			auto password = it.second.getString();
			if (password.length() >= 6) {
				newUser.setString(move(password), "password");
				flags = FillFlags(flags | Password);
			}
		} else if (it.first == "captcha") {
			if (verifyCaptcha(it.second.getString())) {
				flags = FillFlags(flags | Captcha);
			}
		} else if (it.first == "givenName" && it.second.isString() && it.second.size() > 0) {
			newUser.setValue(move(it.second), "givenName");
			flags = FillFlags(flags | GivenName);
		} else if (it.first == "familyName" && it.second.isString() && it.second.size() > 0) {
			newUser.setValue(move(it.second), "familyName");
			flags = FillFlags(flags | FamilyName);
		} else if (it.first == "middleName" && it.second.isString() && it.second.size() > 0) {
			newUser.setValue(move(it.second), "middleName");
		}
	}

	if (flags == All) {
		// make activation code
		auto &data = newUser.emplace("data");
		data.setBytes(valid::makeRandomBytes(6), "code");

		if (auto newUserObj = storage::Worker(userScheme, rctx.storage()).asSystem().create(newUser)) {
			if (auto u = _component->getProvider("local")->initUser(0, _transaction, data::Value(newUserObj))) {

			}
			_component->sendActivationEmail(rctx, newUserObj);

			newUserObj.erase("password");
			newUserObj.erase("data");
			result.setValue(move(newUserObj), "user");
			result.setBool(true, "OK");
			return true;
		} else {
			result.setBool(false, "OK");
			result.setBool(true, "userExist");
			return false;
		}
	} else {
		if ((flags & Password) == None) {
			result.setBool(true, "emptyPassword");
		}
		if ((flags & Email) == None) {
			result.setBool(true, "emptyEmail");
		}
		if ((flags & GivenName) == None) {
			result.setBool(true, "emptyGivenName");
		}
		if ((flags & FamilyName) == None) {
			result.setBool(true, "emptyFamilyName");
		}
		if ((flags & Captcha) == None) {
			result.setBool(false, "captcha");
		}

		result.setBool(false, "OK");
		return false;
	}

	return false;
}

bool LocalAuthHandler::processActivate(Request &rctx, data::Value &result) {
	auto storage = rctx.storage();
	auto userScheme = rctx.server().getScheme("local_users");
	auto &queryData = rctx.getParsedQueryArgs();
	auto &activationCode = queryData.getString("code");

	if (activationCode.empty() || !storage || !userScheme) {
		return false;
	}

	auto d = data::read(base64::decode(activationCode));
	if (!d.isDictionary()) {
		return false;
	}

	auto oid = d.getInteger("id");
	auto &code = d.getBytes("code");

	auto userData = storage::Worker(*userScheme, storage).asSystem().get(oid);
	if (!userData) {
		return false;
	}

	auto isActivated = userData.getBool("isActivated");
	if (!isActivated) {
		auto &d = userData.getValue("data");
		auto &c = d.getBytes("code");
		if (c == code) {
			data::Value patch { pair("isActivated", data::Value(true)) };
			userData = storage::Worker(*userScheme, storage).asSystem().update(oid, patch, true);
			if (auto prov = _component->getProvider("local")) {
				if (auto authUser = prov->authUser(rctx, toString("local-", userData.getInteger("__oid")), move(userData))) {
					uint64_t userId = authUser.getInteger("__oid");
					if (_component->pushAuthUser(rctx, userId)) {
						result.setInteger(userId, "user");
						result.setBool(true, "activated");
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool LocalAuthHandler::processUpdate(Request &rctx, data::Value &input) {
	auto userScheme = rctx.server().getScheme("local_users");
	auto t = storage::Transaction::acquire();
	auto s = ExternalSession::acquire(rctx);

	if (!userScheme || !t) {
		return false;
	}

	data::Value patch;

	if (input.isString("name")) { patch.setValue(input.getValue("name"), "name"); }
	if (input.isString("email")) { patch.setValue(input.getValue("email"), "email"); }
	if (input.isString("familyName")) { patch.setValue(input.getValue("familyName"), "familyName"); }
	if (input.isString("givenName")) { patch.setValue(input.getValue("givenName"), "givenName"); }
	if (input.isString("middleName")) { patch.setValue(input.getValue("middleName"), "middleName"); }
	if (input.isString("picture")) { patch.setValue(input.getValue("picture"), "picture"); }

	if (patch.empty()) {
		return false;
	}

	if (auto prov = _component->getProvider("local")) {
		if (auto user = getLocalUser(s->getUser())) {
			if (auto userData = userScheme->update(t, user, patch)) {
				if (prov->authUser(rctx, toString("local-", userData.getInteger("__oid")), move(userData))) {
					return true;
				}
			}
		}
	}

	return false;
}

int LocalAuthHandler::onLoginInterface(Request &rctx) {
	rctx.setContentType("text/html; charset=UTF-8");
	// print interface
	return DONE;
}

int LocalAuthHandler::onProfileInterface(Request &rctx) {
	rctx.setContentType("text/html; charset=UTF-8");
	// print interface
	return DONE;
}

int LocalAuthHandler::onActivateInterface(Request &rctx) {
	auto &queryData = rctx.getParsedQueryArgs();
	auto &activationCode = queryData.getString("code");

	if (auto provider = _component->getProvider("local")) {
		auto &url = provider->getConfig().getString("activationUrl");
		if (!url.empty()) {
			if (!activationCode.empty()) {
				return rctx.redirectTo(toString(url, "?code=", activationCode));
			} else {
				return rctx.redirectTo(String(url));
			}
		}
	}

	rctx.setContentType("text/html; charset=UTF-8");
	// print interface
	return DONE;
}

int LocalAuthHandler::onPushLogin(Request &rctx, const StringView &userStorage) {
	if (auto d = rctx.storage().get(userStorage)) {
		auto &success = d.getString("success");
		auto &failure = d.getString("failure");

		auto &queryData = rctx.getParsedQueryArgs();
		auto &name = queryData.getString("name");
		auto &passwd = queryData.getString("passwd");

		if (auto u = tryUserLogin(rctx, name, passwd)) {
			if (auto userId = u->getInteger("external_id")) {
				d.setInteger(userId, "user");
				rctx.storage().set(userStorage, d, 300_sec);
				return rctx.redirectTo(String(success));
			}
		}
		return rctx.redirectTo(String(failure));
	}

	return HTTP_NOT_FOUND;
}

bool LocalAuthHandler::verifyCaptcha(const StringView &token) {
	network::Handle h(network::Handle::Method::Post, CAPTCHA_URL);
	h.addHeader("Content-Type: application/x-www-form-urlencoded");
	h.setSendData(toString("secret=", _component->getCaptchaSecret(), "&response=", token));

	auto data = h.performDataQuery();
	return data.getBool("success");
}

data::Value LocalAuthHandler::getLocalUser(int64_t u) {
	auto userScheme = _request.server().getScheme("external_users");
	auto localScheme = _request.server().getScheme("local_users");

	if (!u || !userScheme || !localScheme) {
		return data::Value();
	}

	if (auto t = storage::Transaction::acquire()) {
		if (auto user = userScheme->get(t, u)) {
			if (auto provs = userScheme->getProperty(t, user, "providers")) {
				for (auto &it : provs.asArray()) {
					if (it.getValue("provider") == "local" && StringView(it.getString("id")).starts_with("local-")) {
						if (auto loc = localScheme->get(t, StringView(it.getString("id")).sub(6).readInteger().get(0))) {
							loc.setInteger(it.getInteger("__oid"), "provider");
							return loc;
						}
					}
				}
			}
		}
	}
	return data::Value();
}

NS_SA_EXT_END(trubach)
