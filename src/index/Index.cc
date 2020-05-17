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

#include "Index.h"

#include "IndexChannels.cc"
#include "IndexStat.cc"

NS_SA_EXT_BEGIN(trubach)

class IndexHandlerMap : public HandlerMap {
public:
	IndexHandlerMap() {
		addHandler("Index", Request::Method::Get, "/", SA_HANDLER(IndexHandler));
		addHandler("Group", Request::Method::Get, "/groups/:id", SA_HANDLER(GroupHandler));
		addHandler("Section", Request::Method::Get, "/sections/:id", SA_HANDLER(SectionHandler));
		addHandler("Channel", Request::Method::Get, "/channels/:id", SA_HANDLER(ChannelHandler));
		addHandler("Tools", Request::Method::Get, "/tools", SA_HANDLER(IndexToolsHandler));
		addHandler("Stats", Request::Method::Get, "/stats", SA_HANDLER(IndexStatsHandler));
		addHandler("Stats-Id", Request::Method::Get, "/stats/:id", SA_HANDLER(IndexStatsIdHandler));
	}
};

bool IndexHandlerInterface::isPermitted() {
	_transaction = db::Transaction::acquire(_request.storage());
	_component = _request.server().getComponent<TrubachComponent>();
	if (_component && _transaction) {
		return true;
	}
	return false;
}

data::Value IndexHandlerInterface::getBreadcrumbs() const {
	data::Value breadcrumbs;
	breadcrumbs.addValue(data::Value({
		pair("title", data::Value("Trubach")),
		pair("link", data::Value(toString("/"))),
	}));
	return breadcrumbs;
}

void IndexHandlerInterface::defineTemplateContext(pug::Context &exec) {
	exec.set("breadcrumbs", getBreadcrumbs());

	exec.set("prettySize", [this] (pug::VarStorage &storage, pug::Var *args, size_t argc) -> pug::Var {
		if (argc == 1 && args[0].readValue().getType() == pug::Value::Type::INTEGER) {
			auto val = args[0].readValue().asInteger();
			if (val > int64_t(1_MiB)) {
				return pug::Var(pug::Value(toString(std::setprecision(4), double(val) / 1_MiB, " MiB")));
			} else if (val > int64_t(1_KiB)) {
				return pug::Var(pug::Value(toString(std::setprecision(4), double(val) / 1_KiB, " KiB")));
			} else {
				return pug::Var(pug::Value(toString(val , " bytes")));
			}
		}
		return pug::Var();
	});

	exec.set("prettyTime", [this] (pug::VarStorage &storage, pug::Var *args, size_t argc) -> pug::Var {
		if (argc == 1 && args[0].readValue().getType() == pug::Value::Type::INTEGER) {
			auto val = Time::microseconds(args[0].readValue().asInteger());
			return pug::Var(pug::Value(val.toFormat("%d %h %Y %T")));
		}
		return pug::Var();
	});
	exec.set("toInt", [this] (pug::VarStorage &storage, pug::Var *args, size_t argc) -> pug::Var {
		if (argc == 1) {
			return pug::Var(pug::Value(args[0].readValue().asInteger()));
		}
		return pug::Var();
	});

	StringStream str;
	for (auto &it : _request.getParsedQueryArgs().asDict()) {
		if (it.first != "c") {
			if (!str.empty()) {
				str << "&";
			}
			if (it.second.isArray()) {
				for (auto &iit : it.second.asArray()) {
					str << it.first << "[]=" << iit.asString();
				}
			} else {
				str << it.first << "=" << it.second.asString();
			}
		}
	}
	exec.set("customArgs", data::Value(str.str()));

	if (auto longSession = LongSession::acquire(_request)) {
		exec.set("longSessionUserId", data::Value(longSession->getUser()));
	}

	if (auto session = ExternalSession::acquire(_request)) {
		exec.set("sessionUuid", data::Value(session->getUuid().str()));

		auto u = session->getUser();
		exec.set("sessionUserId", data::Value(u));
		if (u) {
			if (auto user = _component->getUsers()->getExternalUserScheme().get(_transaction, u)) {
				if (auto provs = _component->getUsers()->getExternalUserScheme().getProperty(_transaction, u, "providers")) {
					user.setValue(move(provs), "providers");
				}
				exec.set("user", data::Value(move(user)));
			}
		}

		if (session->getRole() == db::AccessRoleId::Admin) {
			exec.set("admin", data::Value(true));
		}
	}
}

NS_SA_EXT_END(trubach)
