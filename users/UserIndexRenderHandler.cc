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
#include "SPugContext.h"

NS_SA_EXT_BEGIN(trubach)

class IndexRenderHandler : public RequestHandler {
public:
	virtual bool isRequestPermitted(Request & rctx) override;
	virtual int onTranslateName(Request &rctx) override;
};

bool IndexRenderHandler::isRequestPermitted(Request & rctx) {
	return true;
}

int IndexRenderHandler::onTranslateName(Request &rctx) {
	rctx.runPug("templates/auth-user.pug", [&] (pug::Context &exec, const pug::Template &) -> bool {
		if (auto longSession = LongSession::acquire(rctx)) {
			exec.set("longSessionUserId", data::Value(longSession->getUser()));
		}

		if (auto session = ExternalSession::acquire(rctx)) {
			exec.set("sessionUuid", data::Value(session->getUuid().str()));

			auto u = session->getUser();
			exec.set("sessionUserId", data::Value(u));
			auto userScheme = rctx.server().getScheme("external_users");
			if (u && userScheme) {
				auto t = storage::Transaction::acquire(rctx.storage());
				if (auto user = userScheme->get(t, u)) {
					if (auto provs = userScheme->getProperty(t, u, "providers")) {
						user.setValue(move(provs), "providers");
					}
					exec.set("user", data::Value(move(user)));
				}
			}
		}
		return true;
	});
	return DONE;
}

NS_SA_EXT_END(trubach)
