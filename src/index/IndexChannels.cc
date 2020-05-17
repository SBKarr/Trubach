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

static data::Value Index_makeTokenValue(const data::Value &args, const ContinueToken &token) {
	data::Value cursor({
		pair("start", data::Value(token.getStart())),
		pair("end", data::Value(token.getEnd())),
		pair("current", data::Value(args.getString("c"))),
		pair("total", data::Value(token.getTotal())),
	});

	if (token.hasNext()) { cursor.setString(token.encodeNext(), "next"); }
	if (token.hasPrev()) { cursor.setString(token.encodePrev(), "prev"); }

	return cursor;
}

class IndexHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		auto groups = _component->getGroups().select(_transaction, db::Query());
		for (auto &it : groups.asArray()) {
			auto chans = _component->getChannels().select(_transaction, db::Query()
				.select("group", data::Value(it.getInteger("__oid"))).order("subs", db::Ordering::Descending));
			if (!chans.empty()) {
				it.setValue(move(chans), "channels");
			}
		}

		auto chans = _component->getChannels().select(_transaction, db::Query()
				.select("group", db::Comparation::IsNull, data::Value(true)).order("subs", db::Ordering::Descending));

		return _request.runPug("templates/index.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);

			exec.set("groups", move(groups));

			if (!chans.empty()) {
				exec.set("channels", move(chans));
			}
			return true;
		});
	}
};

class IndexToolsHandler : public IndexHandlerInterface {
public:
	virtual bool isPermitted() override {
		if (IndexHandlerInterface::isPermitted()) {
			return _request.isAdministrative();
		}
		return false;
	}

	virtual int onRequest() override {
		return _request.runPug("templates/tools.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);
			return true;
		});
	}
};

class GroupHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		auto id = _params.getInteger("id");

		_group = _component->getGroups().get(_transaction, id);
		if (!_group) {
			return HTTP_NOT_FOUND;
		}

		Map<int64_t, String> channels;
		if (auto c = _component->getGroups().getProperty(_transaction, _group, "channels")) {
			for (auto &it : c.asArray()) {
				channels.emplace(it.getInteger("__oid"), it.getString("title"));
			}
			if (!c.empty()) {
				_group.setValue(move(c), "channels");
			}
		}

		auto token = _queryFields.isString("c") ? ContinueToken(_queryFields.getString("c")) : ContinueToken("publised", 100, true);

		db::Query q;
		q.select("group", data::Value(id)).order("published", db::Ordering::Descending);

		if (auto v = token.performOrdered(_component->getVideos(), _transaction, q)) {
			for (auto &it : v.asArray()) {
				auto cIt = channels.find(it.getInteger("channel"));
				if (cIt != channels.end()) {
					it.setString(cIt->second, "channel");
				}
			}
			if (!v.empty()) {
				_group.setValue(move(v), "videos");
			}
		}

		data::Value freeChannels;
		auto c = _component->getChannels().select(_transaction, db::Query().select("group", db::Comparation::IsNull, data::Value(true)).include("title"));
		for (auto &it : c.asArray()) {
			freeChannels.addValue(data::Value({
				pair("__oid", it.getValue("__oid")),
				pair("title", it.getValue("title")),
			}));
		}

		return _request.runPug("templates/group.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);

			exec.set("group", data::Value(_group));
			exec.set("cursor", Index_makeTokenValue(_queryFields, token));

			if (!freeChannels.empty()) {
				exec.set("freeChannels", move(freeChannels));
			}

			return true;
		});
	}

	virtual data::Value getBreadcrumbs() const override {
		auto b = IndexHandlerInterface::getBreadcrumbs();
		b.addValue(data::Value({
			pair("title", data::Value(_group.getString("title"))),
			pair("link", data::Value(toString("/groups/", _group.getInteger("__oid")))),
		}));
		return b;
	}

protected:
	data::Value _group;
};

class SectionHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		auto id = _params.getInteger("id");

		_section = _component->getSections().get(_transaction, id);
		if (!_section) {
			return HTTP_NOT_FOUND;
		}

		Map<int64_t, String> channels;
		if (auto c = _component->getSections().getProperty(_transaction, _section, "channels")) {
			for (auto &it : c.asArray()) {
				channels.emplace(it.getInteger("__oid"), it.getString("title"));
			}
			_section.setValue(move(c), "channels");
		}


		auto token = _queryFields.isString("c") ? ContinueToken(_queryFields.getString("c")) : ContinueToken("publised", 100, true);

		db::Query q;
		q.select("section", data::Value(id)).order("published", db::Ordering::Descending);

		if (auto v = token.performOrdered(_component->getVideos(), _transaction, q)) {
			for (auto &it : v.asArray()) {
				auto cIt = channels.find(it.getInteger("channel"));
				if (cIt != channels.end()) {
					it.setString(cIt->second, "channel");
				}
			}
			_section.setValue(move(v), "videos");
		}

		return _request.runPug("templates/section.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);

			exec.set("section", data::Value(_section));
			exec.set("cursor", Index_makeTokenValue(_queryFields, token));

			return true;
		});
	}

	virtual data::Value getBreadcrumbs() const override {
		auto b = IndexHandlerInterface::getBreadcrumbs();
		b.addValue(data::Value({
			pair("title", data::Value(_section.getString("title"))),
			pair("link", data::Value(toString("/sections/", _section.getInteger("__oid")))),
		}));
		return b;
	}

protected:
	data::Value _section;
};

class ChannelHandler : public IndexHandlerInterface {
public:
	virtual int onRequest() override {
		auto id = _params.getInteger("id");

		_channel = _component->getChannels().get(_transaction, id);
		if (!_channel) {
			return HTTP_NOT_FOUND;
		}

		auto token = _queryFields.isString("c") ? ContinueToken(_queryFields.getString("c")) : ContinueToken("publised", 100, true);

		db::Query q;
		q.select("channel", data::Value(id)).order("published", db::Ordering::Descending);

		if (auto v = token.performOrdered(_component->getVideos(), _transaction, q)) {
			_channel.setValue(move(v), "videos");
		}

		if (auto v = _component->getSections().get(_transaction, _channel.getInteger("section"))) {
			_channel.setValue(move(v), "section");
		}

		return _request.runPug("templates/channel.pug", [&] (pug::Context &exec, const pug::Template &tpl) -> bool {
			defineTemplateContext(exec);

			exec.set("channel", data::Value(_channel));
			exec.set("cursor", Index_makeTokenValue(_queryFields, token));

			return true;
		});
	}

	virtual data::Value getBreadcrumbs() const override {
		auto b = IndexHandlerInterface::getBreadcrumbs();
		b.addValue(data::Value({
			pair("title", data::Value(_channel.getValue("section").getString("title"))),
			pair("link", data::Value(toString("/sections/", _channel.getValue("section").getInteger("__oid")))),
		}));
		b.addValue(data::Value({
			pair("title", data::Value(_channel.getString("title"))),
			pair("link", data::Value(toString("/channels/", _channel.getInteger("__oid")))),
		}));
		return b;
	}

protected:
	data::Value _channel;
};

NS_SA_EXT_END(trubach)
