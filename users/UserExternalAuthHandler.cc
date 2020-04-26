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

#include "ExternalSession.h"
#include "UsersComponent.h"
#include "Output.h"

NS_SA_EXT_BEGIN(trubach)

class ExternalAuthHandler : public RequestHandler {
public:
	virtual bool isRequestPermitted(Request & rctx) override;
	virtual int onTranslateName(Request &rctx) override;

protected:
	String getErrorUrl(Request &rctx, const StringView &error);
	String getSuccessUrl(Request &rctx, uint64_t user);
	String getExternalRedirectUrl(Request &rctx);

	String getLoginRedirectUrl(Request &rctx, const StringView &provider, const StringView &state);
	String getTryRedirectUrl(Request &rctx, const StringView &provider, const data::Value &, const StringView &state);

	uint64_t processResult(Request &rctx, const StringView &provider, const StringView &type);

	bool initStateToken(Request &rctx, const StringView &token, const StringView &provider, bool isNew, StringView target = StringView());

	data::Value getUserData(Request &rctx, uint64_t userId) const;
	data::Value getProvider(Request &rctx, uint64_t poviderId) const;

	int onUserRequest(Request &rctx, uint64_t user);
	int onProvidersRequest(Request &rctx, uint64_t user);
	int onLastProviderRequest(Request &rctx, uint64_t user);
	int onConnection(Request &rctx);

	virtual int onPage(Request &rctx, const StringView &pageName);
};

bool ExternalAuthHandler::isRequestPermitted(Request & rctx) {
	return true;
}

int ExternalAuthHandler::onTranslateName(Request &rctx) {
	auto h = rctx.server().getComponent<UsersComponent>();
	auto storage = rctx.storage();
	if (!h || !storage) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	if (_subPathVec.size() == 1) {
		if (_subPathVec.front() == "login") {
			auto &args = rctx.getParsedQueryArgs();
			auto &prov = args.getString("provider");
			auto &storage = args.getString("callback");

			auto t = args.getString("target");
			if (!t.empty()) {
				UrlView v(t);
				if (v.host == rctx.getHostname() || v.host.empty()) {
					t = v.get();
				} else {
					t.clear();
				}
			}

			String state(storage.empty() ? UsersComponent::makeStateToken() : StringView(storage).starts_with("auth-") ? storage.substr("auth-"_len) : storage);
			String redirectUrl = getLoginRedirectUrl(rctx, prov, state);

			if (!redirectUrl.empty()) {
				if (!initStateToken(rctx, state, prov, storage.empty(), t)) {
					return HTTP_NOT_FOUND;
				}
				return rctx.redirectTo(move(redirectUrl));
			} else {
				return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_provider")));
			}
		} else if (_subPathVec.front() == "try") {
			String state(UsersComponent::makeStateToken());
			String provider;
			String redirectUrl;
			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				if (auto user = session->getUser()) {
					return rctx.redirectTo(move(getSuccessUrl(rctx, user)));
				}
			}

			if (auto longSession = LongSession::acquire(rctx, h->getKeyPair())) {
				if (auto user = getUserData(rctx, longSession->getUser())) {
					if (auto prov = getProvider(rctx, user.getInteger("lastProvider"))) {
						provider = prov.getString("provider");
						redirectUrl = getTryRedirectUrl(rctx, prov.getString("provider"), prov, state);
					}
				}
			}

			if (redirectUrl.empty()) {
				auto &args = rctx.getParsedQueryArgs();
				provider = args.getString("provider");

				redirectUrl = getTryRedirectUrl(rctx, provider, data::Value(), state);
			}

			auto successUrl =  toString(_originPath, "/success");
			if (StringView(redirectUrl).starts_with(StringView(successUrl))) {
				StringView r(redirectUrl); r += successUrl.size() + "?user="_len;
				if (auto userId = r.readInteger().get()) {
					if (auto h = rctx.server().getComponent<UsersComponent>()) {
						if (h->pushAuthUser(rctx, userId)) {
							return rctx.redirectTo(move(getSuccessUrl(rctx, userId)));
						}
					}
				}
				return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_provider")));
			}

			if (!redirectUrl.empty()) {
				initStateToken(rctx, state, provider, true);
				return rctx.redirectTo(move(redirectUrl));
			} else {
				return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_provider")));
			}
		} else if (_subPathVec.front() == "callback") {
			auto &args = rctx.getParsedQueryArgs();
			String provider;
			String type;
			data::Value stateData;
			auto &state = args.getString("state");
			if (args.isString("provider")) {
				provider = args.getString("provider");
			} else {
				if (state.empty()) {
					return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_state")));
				}

				auto dataKey = toString("auth-", state);
				stateData = storage.get(dataKey);
				if (!stateData.isDictionary()) {
					return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_state")));
				}

				provider = stateData.getString("provider");
				type = stateData.getString("type");
				if (type.empty()) {
					storage.clear(dataKey);
				}
			}

			if (provider.empty()) {
				return rctx.redirectTo(move(getErrorUrl(rctx, "auth_invalid_state")));
			}

			if (type.empty()) {
				auto target = stateData.getString("target");
				if (!target.empty()) {
					UrlView v(target);
					if (v.host == rctx.getHostname() || v.host.empty()) {
						String query;
						if (auto user = processResult(rctx, provider, String())) {
							if (v.query.empty()) {
								query = toString("user=", user);
							} else {
								query = toString(v.query, "&user=", user);
							}
						} else {
							if (v.query.empty()) {
								query = "error=auth_failed";
							} else {
								query = toString(v.query, "&error=", "auth_failed");
							}
						}
						v.query = query;
						return rctx.redirectTo(v.get());
					}
				} else {
					if (auto user = processResult(rctx, provider, String())) {
						return rctx.redirectTo(move(getSuccessUrl(rctx, user)));
					} else {
						return rctx.redirectTo(move(getErrorUrl(rctx, "auth_failed")));
					}
				}
			} else {
				auto success = stateData.getString("success");
				auto failure = stateData.getString("failure");

				if (auto user = processResult(rctx, provider, toString("auth-", state))) {
					return rctx.redirectTo(success.empty() ? move(getSuccessUrl(rctx, user)) : move(success));
				} else {
					return rctx.redirectTo(failure.empty() ? move(getErrorUrl(rctx, "auth_failed")) : move(failure));
				}
			}
		} else if (_subPathVec.front() == "cancel") {
			if (auto longSession = LongSession::acquire(rctx, h->getKeyPair())) {
				longSession->cancel();
			}

			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				session->cancel();
			}

			auto &target = rctx.getParsedQueryArgs().getValue("target");
			if (target.isString()) {
				if (StringView(target.getString()).starts_with(StringView(rctx.getFullHostname()))) {
					return rctx.redirectTo(String(target.getString()));
				} else {
					return HTTP_NOT_IMPLEMENTED;
				}
			}

			return onPage(rctx, "cancel");
		} else if (_subPathVec.front() == "exit") {
			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				session->cancel();
			}

			auto &target = rctx.getParsedQueryArgs().getValue("target");
			if (target.isString()) {
				if (StringView(target.getString()).starts_with(StringView(rctx.getFullHostname()))) {
					return rctx.redirectTo(String(target.getString()));
				} else {
					return HTTP_NOT_IMPLEMENTED;
				}
			}
		} else if (_subPathVec.front() == "user") {
			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				if (auto user = session->getUser()) {
					return onUserRequest(rctx, user);
				}
			}
			return HTTP_NOT_FOUND;
		} else if (_subPathVec.front() == "providers") {
			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				if (auto user = session->getUser()) {
					return onProvidersRequest(rctx, user);
				}
			}
			return HTTP_NOT_FOUND;
		} else if (_subPathVec.front() == "last_provider") {
			if (auto session = ExternalSession::acquire(rctx, h->getKeyPair())) {
				if (auto user = session->getUser()) {
					return onLastProviderRequest(rctx, user);
				}
			}
			return HTTP_NOT_FOUND;
		} else if (_subPathVec.front() == "on_connection") {
			return onConnection(rctx);
		} else {
			return onPage(rctx, _subPathVec.front());
		}
	}

	return HTTP_NOT_FOUND;
}

String ExternalAuthHandler::getErrorUrl(Request &rctx, const StringView &error) {
	return toString(_originPath, "/error?error=", error);
}

String ExternalAuthHandler::getSuccessUrl(Request &rctx, uint64_t user) {
	return toString(_originPath, "/success?user=", user);
}

String ExternalAuthHandler::getExternalRedirectUrl(Request &rctx) {
	return toString(rctx.getFullHostname(), _originPath, "/callback");
}

String ExternalAuthHandler::getLoginRedirectUrl(Request &rctx, const StringView &provider, const StringView &state) {
	if (auto h = rctx.server().getComponent<UsersComponent>()) {
		return h->onLogin(rctx, provider, state, getExternalRedirectUrl(rctx));
	}
	return String();
}

String ExternalAuthHandler::getTryRedirectUrl(Request &rctx, const StringView &provider, const data::Value &data, const StringView &state) {
	if (auto h = rctx.server().getComponent<UsersComponent>()) {
		return h->onTry(rctx, provider, data, state, getExternalRedirectUrl(rctx));
	}
	return String();
}

uint64_t ExternalAuthHandler::processResult(Request &rctx, const StringView &provider, const StringView &storage) {
	if (auto h = rctx.server().getComponent<UsersComponent>()) {
		if (auto user = h->onResult(rctx, provider, getExternalRedirectUrl(rctx))) {
			if (storage.empty()) {
				if (h->pushAuthUser(rctx, user)) {
					return user;
				}
			} else {
				if (auto d = rctx.storage().get(storage)) {
					d.setInteger(user, "user");
					rctx.storage().set(storage, d, 300_sec);
					return user;
				}
			}
		}
	}
	return 0;
}

bool ExternalAuthHandler::initStateToken(Request &rctx, const StringView &token, const StringView &provider, bool isNew, StringView target) {
	String tokenKey = StringView(token).starts_with("auth-") ? token.str() : toString("auth-", token);
	data::Value d;
	if (!isNew) {
		d = rctx.storage().get(tokenKey);
		if (!d.isDictionary()) {
			return false;
		}
	}

	d.setBool(true, "auth");
	d.setString(provider, "provider");

	if (!target.empty()) {
		d.setString(target, "target");
	}

	rctx.storage().set(tokenKey, d, 720_sec);

	return true;
}

data::Value ExternalAuthHandler::getUserData(Request &rctx, uint64_t u) const {
	auto userScheme = rctx.server().getScheme("external_users");
	if (u && userScheme) {
		return storage::Worker(*userScheme, rctx.storage()).asSystem().get(u);
	}
	return data::Value();
}

data::Value ExternalAuthHandler::getProvider(Request &rctx, uint64_t poviderId) const {
	auto provScheme = rctx.server().getScheme("auth_providers");
	if (poviderId && provScheme) {
		return storage::Worker(*provScheme, rctx.storage()).asSystem().get(poviderId);
	}
	return data::Value();
}

int ExternalAuthHandler::onUserRequest(Request &rctx, uint64_t user) {
	if (auto data = getUserData(rctx, user)) {
		output::writeResourceData(rctx, move(data), data::Value());
		return DONE;
	}

	return HTTP_NOT_FOUND;
}

int ExternalAuthHandler::onProvidersRequest(Request &rctx, uint64_t user) {
	auto userScheme = rctx.server().getScheme("external_users");
	if (userScheme && user) {
		if (auto data = storage::Worker(*userScheme, rctx.storage()).asSystem().getField(user, "providers")) {
			output::writeResourceData(rctx, move(data), data::Value());
			return DONE;
		}
	}
	return HTTP_NOT_FOUND;
}

int ExternalAuthHandler::onLastProviderRequest(Request &rctx, uint64_t user) {
	auto userScheme = rctx.server().getScheme("external_users");
	if (userScheme && user) {
		if (auto data = storage::Worker(*userScheme, rctx.storage()).asSystem().getField(user, "lastProvider")) {
			output::writeResourceData(rctx, move(data), data::Value());
			return DONE;
		}
	}
	return HTTP_NOT_FOUND;
}

int ExternalAuthHandler::onConnection(Request &rctx) {
	if (auto h = rctx.server().getComponent<UsersComponent>()) {
		auto &queryData = rctx.getParsedQueryArgs();
		auto &userStorage = queryData.getString("storage");

		if (auto d = rctx.storage().get(userStorage)) {
			auto userId = d.getInteger("user");
			if (auto u = storage::Worker(h->getExternalUserScheme(), rctx.storage()).asSystem().get(userId)) {
				auto hash = string::Sha512::perform(rctx.getUseragentIp(),
						rctx.getRequestHeaders().at("user-agent"), Time::now().toHttp(), valid::makeRandomBytes(16));

				d.setBytes(Bytes(hash.data(), hash.data() + hash.size()), "secret");
				rctx.storage().set(userStorage, d, 300_sec);
				return rctx.redirectTo(toString(h->getBindAuth(), "connected?secret=", base64url::encode(hash), "&storage=", userStorage));
			}
		}
	}
	return HTTP_NOT_FOUND;
}

int ExternalAuthHandler::onPage(Request &rctx, const StringView &pageName) {
	auto h = rctx.getResponseHeaders();
	h.emplace("Cache-Control", "no-cache, no-store, must-revalidate");
	h.emplace("Pragma", "no-cache");
	h.emplace("Expires", "0");

	if (pageName == "cancel") {
		rctx.setContentType("text/html; charset=UTF-8");
		rctx << "<!DOCTYPE html><html><head><title>Successful exit</title><script>opener.AuthExitCallback(location.href);</script></head><body>"
			"<h1>Session canceled successfully</h1>"
			"<p><a href=\"/auth/index\">Return</a></p>"
			"</body></html>\n";
		return DONE;
	} else if (pageName == "error") {
		auto &args = rctx.getParsedQueryArgs();
		rctx.setContentType("text/html; charset=UTF-8");
		/* rctx << "<!DOCTYPE html><html><head><title>Authorization error</title></head><body>"
			"<h1>Authorization error: " << args.getString("error") << "</h1>"
			"<script>opener.AuthErrorCallback('" << args.getString("error") << "');</script>"
			"<p><a href=\"/auth/index\">Return</a></p>"
			"</body></html>\n"; */
		rctx << "<!DOCTYPE html><html><head><title>Ошибка авторизации</title></head><body>"
				"<h1>Пользователь с таким e-mail адресом уже зарегистрирован. Пожалуйста, привяжите социальную сеть в личном кабинете</h1>"
				"<script>opener.AuthErrorCallback('" << args.getString("error") << "');</script>"
				"</body></html>\n";
		return DONE;
	} else if (pageName == "success") {
		rctx.setContentType("text/html; charset=UTF-8");
		rctx << "<!DOCTYPE html><html><head><title>Authorization success</title></head><body>"
			"<h1>Authorization success!</h1>"
			"<script>opener.AuthSuccessCallback(location.href);</script>"
			"<p><a href=\"/auth/index\">Return</a></p>"
			"</body></html>\n";
		return DONE;
	} else if (pageName == "connect") {
		rctx.setContentType("text/html; charset=UTF-8");
		if (auto h = rctx.server().getComponent<UsersComponent>()) {
			auto &args = rctx.getParsedQueryArgs();

			String token = args.getString("storage");
			if (!token.empty()) {
				auto d = rctx.storage().get(token);
				if (!d.isDictionary() || d.getString("type") != "connection") {
					rctx << "<!DOCTYPE html><html><head><title>Device connection</title></head><body>"
							"<p style=\"color: red;\">Invalid storage token</p></body></html>\n";
					return DONE;
				} else {
					d.setString(toString(h->getBindAuth(), "on_connection?storage=", token), "success");
					d.setString(toString(h->getBindAuth(), "connect?storage=", token, "&auth_error"), "failure");
					rctx.storage().set(token, d);
				}
			} else {
				rctx << "<!DOCTYPE html><html><head><title>Device connection</title></head><body>"
						"<p style=\"color: red;\">Invalid storage token</p></body></html>\n";
				return DONE;
			}

			rctx << "<!DOCTYPE html><html><head><title>Device connection</title></head><body onload=\"onLoadFunc();\">"
					"<script>function onLoadFunc() { console.log(window.location.href); }</script>";
			if (args.hasValue("auth_error")) {
				rctx << "<p style=\"color: red;\">Auth error</p>";
			}
			rctx << "<form action=\"" << h->getBindLocal() << "push_login/" << token << "\">"
					"<input placeholder=\"Name\" name=\"name\" type=\"text\">"
					"<input placeholder=\"Password\" name=\"passwd\" type=\"password\">"
					"<input type=\"submit\">"
					"</form>"
					"<p><a target=\"_self\" href=\"" << h->getBindAuth() << "login?provider=google&storage=" << token << "\">Sign In with Google</a></p>"
					"</body></html>\n";
		}
		return DONE;
	} else if (pageName == "connected") {
		rctx.setContentType("text/html; charset=UTF-8");
		rctx << "<!DOCTYPE html><html><head><title>Device connection</title></head>"
				"<body onload=\"onLoadFunc();\"><script>"
				"function onLoadFunc() { console.log(window.location.href); window.close(); }"
				"</script><h1>Success</h1></body></html>\n";
		return DONE;
	}

	return HTTP_NOT_FOUND;
}

NS_SA_EXT_END(trubach)
