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

NS_SA_EXT_BEGIN(trubach)

class IndexStatsHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		auto s = _component->getChansnap().select(_transaction, db::Query());

		return _request.runPug("templates/stats.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);

			exec.set("stats", move(s));
			return true;
		});
	}

	virtual data::Value getBreadcrumbs() const override {
		auto b = IndexHandlerInterface::getBreadcrumbs();
		b.addValue(data::Value({
			pair("title", data::Value("Статистика")),
			pair("link", data::Value("/stats")),
		}));
		return b;
	}
};

class IndexStatsIdHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		if (auto s = _component->getChansnap().get(_transaction, _params.getInteger("id"))) {
			auto ord = _request.getParsedQueryArgs().getString("ord");
			auto sort = _request.getParsedQueryArgs().getString("sort");

			if (ord != "subs" && ord != "nviews" && ord != "nvideos") {
				ord = "subs";
			}

			db::Ordering ordering = db::Ordering::Descending;
			if (sort == "asc") {
				ordering = db::Ordering::Ascending;
			}

			auto chanstat = _component->getChanstat().select(_transaction, db::Query()
				.select("snap", data::Value(s.getInteger("__oid"))).order(ord, ordering));
			for (auto &it : chanstat.asArray()) {
				if (auto cId = it.getInteger("chan")) {
					if (auto chan = _component->getChannels().get(_transaction, cId, {"title", "id"})) {
						it.setValue(move(chan), "chan");
					}
				}
			}

			_id = s.getInteger("__oid");
			_date = s.getInteger("date");

			return _request.runPug("templates/stats.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
				defineTemplateContext(exec);

				exec.set("stat", move(s));
				exec.set("channels", move(chanstat));
				exec.set("ord", data::Value(ord));
				exec.set("sort", data::Value(ordering == db::Ordering::Ascending ? "asc" : "desc"));
				return true;
			});
		}
		return HTTP_NOT_FOUND;
	}

	virtual data::Value getBreadcrumbs() const override {
		auto b = IndexHandlerInterface::getBreadcrumbs();
		b.addValue(data::Value({
			pair("title", data::Value("Статистика")),
			pair("link", data::Value("/stats")),
		}));
		b.addValue(data::Value({
			pair("title", data::Value(Time::microseconds(_date).toHttp())),
			pair("link", data::Value(toString("/stats/", _id))),
		}));
		return b;
	}

protected:
	int64_t _id = 0;
	int64_t _date = 0;
};

NS_SA_EXT_END(trubach)
