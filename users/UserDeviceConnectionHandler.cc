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

NS_SA_EXT_BEGIN(trubach)

class DeviceConnectionHandler : public DataHandler {
public:
	DeviceConnectionHandler();

	virtual bool isRequestPermitted(Request & rctx) override;
	virtual bool processDataHandler(Request &, data::Value &result, data::Value &input) override;

protected:
	bool onIntent(Request &, data::Value &result, const data::Value &input);
	bool onToken(Request &, data::Value &result, const StringView &provider, const data::Value &input);
	bool onLogin(Request &, data::Value &result, const data::Value &input);
	bool onConnect(Request &, data::Value &result, const data::Value &input);
	bool onTouch(Request &, data::Value &result, const data::Value &input);
	bool onCancel(Request &, data::Value &result, const data::Value &input);

	data::Value makeConnection(Request &rctx, int64_t userId, int64_t appId, const StringView &token);

	int64_t _appId = 0;
	UsersComponent *_component = nullptr;
};

DeviceConnectionHandler::DeviceConnectionHandler() {
	_maxRequestSize = 16_KiB;
	_maxVarSize = 4_KiB;
	_allow = AllowMethod::Post | AllowMethod::Get;
	_required = db::InputConfig::Require::Data;
}

bool DeviceConnectionHandler::isRequestPermitted(Request & rctx) {
	if (auto h = rctx.server().getComponent<UsersComponent>()) {
		_component = h;
		_appId = h->isValidAppRequest(rctx);
		if (_appId) {
			if (_subPath == "/intent") {
				return rctx.getMethod() == Request::Post;
			} else if (_subPath == "/token") {
				return rctx.getMethod() == Request::Post;
			} else if (_subPath == "/login") {
				return rctx.getMethod() == Request::Post;
			} else if (_subPath == "/connect") {
				return rctx.getMethod() == Request::Get;
			} else if (_subPath == "/touch") {
				return rctx.getMethod() == Request::Post;
			} else if (_subPath == "/cancel") {
				return rctx.getMethod() == Request::Get;
			}
		}
	}
	rctx.setStatus(HTTP_NOT_FOUND);
	return false;
}

bool DeviceConnectionHandler::processDataHandler(Request &rctx, data::Value &result, data::Value &input) {
	if (_subPath == "/intent") {
		return onIntent(rctx, result, input);
	} else if (_subPath == "/token") {
		return onToken(rctx, result, rctx.getParsedQueryArgs().getString("provider"), input);
	} else if (_subPath == "/login") {
		return onLogin(rctx, result, input);
	} else if (_subPath == "/connect") {
		return onConnect(rctx, result, rctx.getParsedQueryArgs());
	} else if (_subPath == "/touch") {
		return onTouch(rctx, result, input);
	} else if (_subPath == "/cancel") {
		return onCancel(rctx, result, rctx.getParsedQueryArgs());
	}
	return false;
}

bool DeviceConnectionHandler::onIntent(Request &rctx, data::Value &result, const data::Value &input) {
	auto &token = input.getString("token");
	auto headers = rctx.getRequestHeaders();

	auto agent = headers.at("user-agent");
	auto app = headers.at("X-ApplicationName");
	auto version = headers.at("X-ApplicationVersion");

	if (!agent.empty() || !app.empty() || !version.empty()) {
		auto intent = toString("auth-", UsersComponent::makeStateToken());
		rctx.storage().set(intent, data::Value({
			pair("type", data::Value("connection")),
			pair("token", data::Value(token)),
			pair("agent", data::Value(agent)),
			pair("app", data::Value(app)),
			pair("appId", data::Value(_appId)),
			pair("version", data::Value(version)),
			pair("success", data::Value(toString(_component->getBindAuth(), "on_connection?storage=", intent))),
			pair("failure", data::Value(toString(_component->getBindAuth(), "connect?storage=", intent, "&auth_error"))),
		}), 720_sec);

		result.setString(intent, "storage");
		return true;
	}

	return false;
}

bool DeviceConnectionHandler::onToken(Request &rctx, data::Value &result, const StringView &provider, const data::Value &input) {
	auto &token = input.getString("token");
	auto &storage = input.getString("storage");

	if (auto d = rctx.storage().get(storage)) {
		auto appId = d.getInteger("appId");
		if (appId && appId == _appId) {
			if (auto user = _component->onToken(rctx, provider, token)) {
				auto hash = string::Sha512::perform(rctx.getUseragentIp(),
						rctx.getRequestHeaders().at("user-agent"), Time::now().toHttp(), valid::makeRandomBytes(16));

				d.setBytes(Bytes(hash.data(), hash.data() + hash.size()), "secret");
				d.setInteger(user, "user");
				rctx.storage().set(storage, d, 300_sec);

				result.setString(base64url::encode(hash), "secret");
				return true;
			} else {
				messages::error("Connection", "Fail to authorize user", data::Value(token));
			}
		} else {
			messages::error("Connection", "Invalid app id", data::Value(_appId));
		}
	} else {
		messages::error("Connection", "No storage", data::Value(storage));
	}

	return false;
}

bool DeviceConnectionHandler::onLogin(Request &rctx, data::Value &result, const data::Value &input) {
	auto &name = input.getString("email");
	auto &passwd = input.getString("password");

	auto userScheme = rctx.server().getScheme("local_users");
	auto storage = rctx.storage();
	if (!userScheme || !storage) {
		return false;
	}

	if (name.empty() || passwd.empty()) {
		messages::error("Auth", "Name or password is not specified", data::Value({
			pair("Doc", data::Value("You should specify 'email' and 'password' variables in request"))
		}));
		return false;
	}

	auto user = User::get(rctx.storage(), *userScheme, name, passwd);
	if (!user) {
		messages::error("Auth", "Invalid username or password");
		return false;
	}

	auto &userData = user->getData();

	bool isActivated = user->getBool("isActivated");
	if (isActivated) {
		if (auto prov = _component->getProvider("local")) {
			if (auto authUser = prov->authUser(rctx, toString("local-", userData.getInteger("__oid")), move(userData))) {
				if (auto conn = makeConnection(rctx, authUser.getInteger("__oid"), _appId, StringView())) {
					result.setValue(move(conn), "user");
					return true;
				}
			}
		}
	}

	messages::error("Auth", "Fail to create connection");
	return false;
}

bool DeviceConnectionHandler::onConnect(Request &rctx, data::Value &result, const data::Value &input) {
	auto headers = rctx.getRequestHeaders();
	auto &token = input.getString("storage");
	auto &sec = input.getString("secret");
	if (auto d = rctx.storage().get(token)) {
		auto secData = base64::decode(sec);
		auto &storageSec = d.getBytes("secret");
		if (secData.empty() || storageSec.size() != secData.size() || memcmp(secData.data(), storageSec.data(), secData.size()) != 0) {
			return false;
		}

		rctx.storage().clear(token);
		int64_t user = d.getInteger("user");
		int64_t appId = d.getInteger("appId");
		auto token = d.getString("token");
		auto agent = headers.at("user-agent");
		auto app = headers.at("X-ApplicationName");
		auto version = headers.at("X-ApplicationVersion");

		if (d.getString("agent") == agent && d.getString("app") == app && appId == _appId && d.getString("version") == version) {
			if (auto conn = makeConnection(rctx, user, appId, token)) {
				result.setValue(move(conn), "user");
				return true;
			}
		}
	}
	return false;
}

bool DeviceConnectionHandler::onTouch(Request &rctx, data::Value &result, const data::Value &input) {
	auto headers = rctx.getRequestHeaders();
	auto secret = input.getBytes("secret");
	auto secretData = data::read(secret);

	int64_t secretConnId = secretData.getInteger("id");
	int64_t secretAppId = secretData.getInteger("app");
	Bytes secretBytes = secretData.getBytes("secret");

	if (_appId != secretAppId) {
		return false;
	}

	if (auto conn = storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().get(secretConnId, {"app", "user", "data", "secret", "sig"})) {
		int64_t userId = conn.getInteger("user");
		int64_t appId = conn.getInteger("app");
		if (conn.getBytes("secret") == secretBytes && _appId == appId) {
			auto userData = storage::Worker(_component->getExternalUserScheme(), rctx.storage()).asSystem().get(userId, {"ctime", "email", "familyName", "fullName", "givenName", "locale", "picture"});
			auto appData = storage::Worker(_component->getApplicationScheme(), rctx.storage()).asSystem().get(appId, {"name", "secret"});

			if (!userData || !appData) {
				storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().remove(secretConnId);
				return false;
			}

			auto &sig = conn.getString("sig");
			auto &sec = appData.getValue("secret");
			if (sec.isString()) {
				if (sig != sec.getString()) {
					storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().remove(secretConnId);
					return false;
				}
			} else if (sec.isArray()) {
				bool found = false;
				for (auto &it : sec.asArray()) {
					if (it.getString() == sig) {
						found = true;
						break;
					}
				}

				if (!found) {
					storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().remove(secretConnId);
					return false;
				}
			}

			auto &d = conn.getValue("data");

			auto token = input.getString("token");
			auto agent = headers.at("user-agent");
			auto version = headers.at("X-ApplicationVersion");

			data::Value patch;
			if (!token.empty() && token != d.getString("token")) {
				patch.setString(token, "token");
			}
			if (!agent.empty() && agent != d.getString("agent")) {
				patch.setString(agent, "agent");
			}
			if (!version.empty() && version != d.getString("version")) {
				patch.setString(agent, "version");
			}

			if (!patch.empty()) {
				storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().update(secretConnId, data::Value({
					pair("data", move(patch))
				}));
			}

			result.setValue(move(userData), "user");

			return true;
		}
	}
	return false;
}

bool DeviceConnectionHandler::onCancel(Request &rctx, data::Value &result, const data::Value &input) {
	if (auto c = _component->isValidSecret(rctx, input.getValue("secret"))) {
		return storage::Worker(_component->getConnectionScheme(), rctx.storage()).asSystem().remove(c.getInteger("conn"));
	}
	return false;
}

data::Value DeviceConnectionHandler::makeConnection(Request &rctx, int64_t userId, int64_t appId, const StringView &token) {
	return _component->makeConnection(rctx, userId, appId, token);
}

NS_SA_EXT_END(trubach)
