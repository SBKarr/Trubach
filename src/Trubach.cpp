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

#include "Define.h"
#include "Trubach.h"
#include "UsersComponent.h"

#include "Admin.cc"
#include "Index.cc"
#include "TGBot.cc"
#include "Hub.cc"

NS_SA_EXT_BEGIN(trubach)

TrubachComponent::TrubachComponent(Server &serv, const String &name, const data::Value &dict)
: ServerComponent(serv, name, dict) {
	using namespace db;

	auto users = serv.addComponent(new UsersComponent(serv, "Users", dict));

	_users = users;

	exportValues(_channels, _videos, _groups, _sections, _chanstat, _chansnap, _notifiers);

	_channels.define(Vector<Field>({
		Field::Integer("ctime", Flags::AutoCTime),
		Field::Integer("mtime", Flags::AutoMTime),

		Field::Integer("created"),
		Field::Text("id", Flags::Indexed, Flags::Unique),
		Field::Text("uploads", Flags::Indexed, Flags::Unique),
		Field::Text("etag"),
		Field::Text("title"),
		Field::Text("desc"),
		Field::Integer("subs", Flags::Indexed),
		Field::Integer("nvideos"),
		Field::Integer("nviews"),

		Field::Extra("thumbs", Vector<Field>({
			Field::Text("default", MaxLength(1_KiB)),
			Field::Text("high", MaxLength(1_KiB)),
			Field::Text("medium", MaxLength(1_KiB)),
		})),

		Field::Data("data", Flags::ForceExclude),
		Field::Set("videos", _videos),
		Field::Object("group", _groups, RemovePolicy::Null),

		Field::Integer("subscribed", data::Value(0), Flags::Indexed),
	}));

	_videos.define(Vector<Field>({
		Field::Integer("ctime", Flags::AutoCTime),
		Field::Integer("mtime", Flags::AutoMTime),

		Field::Text("id", Flags::Indexed, Flags::Unique),
		Field::Text("itemId"),
		Field::Integer("published"),

		Field::Text("etag"),
		Field::Text("title"),
		Field::Text("desc"),
		Field::Data("data", Flags::ForceExclude),

		Field::Extra("thumbs", Vector<Field>({
			Field::Text("default", MaxLength(1_KiB)),
			Field::Text("high", MaxLength(1_KiB)),
			Field::Text("medium", MaxLength(1_KiB)),
			Field::Text("standard", MaxLength(1_KiB)),
		})),

		Field::Object("channel", _channels, RemovePolicy::Null),
		Field::Object("group", _groups, RemovePolicy::Null, DefaultFn([this] (const data::Value &val) -> data::Value {
			if (auto c = _channels.get(Transaction::acquireIfExists(), val.getInteger("channel"), "group")) {
				if (auto g = c.getInteger("group")) {
					return data::Value(g);
				}
			}
			return data::Value();
		}))
	}));

	_groups.define(Vector<Field>({
		Field::Text("name", Transform::Alias),
		Field::Text("title", MaxLength(1_KiB)),
		Field::Set("channels", _channels),
	}));

	_sections.define(Vector<Field>({
		Field::Text("name", Transform::Alias),
		Field::Text("title", MaxLength(1_KiB)),
		Field::Set("channels", _channels, Flags::Reference),
	}));

	_chanstat.define(Vector<Field>({
		Field::Integer("snap", Flags::Indexed),
		Field::Integer("chan", Flags::Indexed),

		Field::Integer("subs", Flags::Indexed),
		Field::Integer("nvideos", Flags::Indexed),
		Field::Integer("nviews", Flags::Indexed),
	}));

	_chansnap.define(Vector<Field>({
		Field::Integer("date", Flags::AutoCTime, Flags::Indexed),
	}));

	_notifiers.define(Vector<Field>({
		Field::Integer("role", Flags::Indexed),
		Field::Integer("id", Flags::Indexed),
		Field::Text("channel", MinLength(1)),
		Field::Text("firstName", MinLength(1)),
		Field::Text("lastName", MinLength(1)),
		Field::Text("username", MinLength(1)),
	}));

	_users->init(this);
}

void TrubachComponent::onChildInit(Server &serv) {
	ServerComponent::onChildInit(serv);

	addCommand("notify-all", [this] (const StringView &str) -> data::Value {
		StringView r(str);
		r.skipChars<StringView::CharGroup<CharGroupId::WhiteSpace>>();

		if (auto t = db::Transaction::acquire()) {
			t.performAsSystem([&] () -> bool {
				auto n = _notifiers.select(t, db::Query());
				for (auto &it : n.asArray()) {
					sendTgMessage(it, r, NotificationFormat::Plain);
				}
				return true;
			});
			return data::Value(true);
		}
		return data::Value("Fail to perform command");
	});

	addCommand("subscribe-all", [this] (const StringView &str) -> data::Value {
		if (auto t = db::Transaction::acquire()) {
			data::Value success;
			data::Value errors;
			auto c = _channels.select(t, db::Query());
			for (auto &it : c.asArray()) {
				if (!subscribeChannel(it)) {
					errors.addValue(data::Value({
						pair("__oid", it.getValue("__oid")),
						pair("title", it.getValue("title"))
					}));
				} else {
					success.addValue(data::Value({
						pair("__oid", it.getValue("__oid")),
						pair("title", it.getValue("title"))
					}));
				}
			}
			return data::Value({
				pair("errors", move(errors)),
				pair("success", move(success))
			});
		}
		return data::Value("Fail to perform command");
	});

	addCommand("unsubscribe-all", [this] (const StringView &str) -> data::Value {
		if (auto t = db::Transaction::acquire()) {
			data::Value success;
			data::Value errors;
			auto c = _channels.select(t, db::Query());
			for (auto &it : c.asArray()) {
				if (!unsubscribeChannel(it)) {
					errors.addValue(data::Value({
						pair("__oid", it.getValue("__oid")),
						pair("title", it.getValue("title"))
					}));
				} else {
					success.addValue(data::Value({
						pair("__oid", it.getValue("__oid")),
						pair("title", it.getValue("title"))
					}));
				}
			}
			return data::Value({
				pair("errors", move(errors)),
				pair("success", move(success))
			});
		}
		return data::Value("Fail to perform command");
	});

	serv.addHandler("/", new IndexHandlerMap);
	serv.addHandler("/admin/", new AdminHandlerMap);

	serv.addHandler(toString("/tg/", TG_BOT), SA_HANDLER(TgBotHandler));
	serv.addHandler(toString("/hub/", HUB_SECRET), SA_HANDLER(HubHandler));

	serv.addResourceHandler("/api/v1/sections/", _sections);
	serv.addResourceHandler("/api/v1/groups/", _groups);

	Task::perform(_server, [&] (Task &task) {
		task.addExecuteFn([this] (const Task &task) -> bool {
			updateTgIntegration();
			return true;
		});
	});
}

void TrubachComponent::onStorageTransaction(db::Transaction &t) {
	if (auto req = Request(mem::request())) {
		if (auto s = ExternalSession::get(req)) {
			if (toInt(t.getRole()) < toInt(s->getRole())) {
				t.setRole(s->getRole());
			}
		}
	}
}

UsersComponent *TrubachComponent::getUsers() const {
	return _users;
}

const db::Scheme &TrubachComponent::getChannels() const {
	return _channels;
}

const db::Scheme &TrubachComponent::getVideos() const {
	return _videos;
}

const db::Scheme &TrubachComponent::getGroups() const {
	return _groups;
}

const db::Scheme &TrubachComponent::getSections() const {
	return _sections;
}

const db::Scheme &TrubachComponent::getChanstat() const {
	return _chanstat;
}

const db::Scheme &TrubachComponent::getChansnap() const {
	return _chansnap;
}

const db::Scheme &TrubachComponent::getNotifiers() const {
	return _notifiers;
}

bool TrubachComponent::subscribeChannel(const data::Value &ch) const {
	if (ch.getInteger("subscribed") <= int64_t(Time::now().toMicros())) {
		NetworkHandle h;
		h.init(NetworkHandle::Method::Post, toString("https://pubsubhubbub.appspot.com"));

		StringStream data;
		data << "hub.mode=subscribe"
				<< "&hub.callback=" << string::urlencode(toString(HTTP_ADDRESS, "/hub/")) << HUB_SECRET
				<< "&hub.topic=" << string::urlencode("https://www.youtube.com/xml/feeds/videos.xml?channel_id=") << ch.getString("id");

		h.setSendData(data.weak());
		h.addHeader("Content-Type", "application/x-www-form-urlencoded");
		h.perform();

		auto code = h.getResponseCode();
		return code == 202;
	}
	return false;
}

bool TrubachComponent::unsubscribeChannel(const data::Value &ch) const {
	if (ch.getInteger("subscribed") > int64_t(Time::now().toMicros())) {
		NetworkHandle h;
		h.init(NetworkHandle::Method::Post, toString("https://pubsubhubbub.appspot.com"));

		StringStream data;
		data << "hub.mode=unsubscribe"
				<< "&hub.callback=" << string::urlencode(toString(HTTP_ADDRESS, "/hub/")) << HUB_SECRET
				<< "&hub.topic=" << string::urlencode("https://www.youtube.com/xml/feeds/videos.xml?channel_id=") << ch.getString("id");

		h.setSendData(data.weak());
		h.addHeader("Content-Type", "application/x-www-form-urlencoded");
		h.perform();

		auto code = h.getResponseCode();
		return code == 202;
	}
	return false;
}

SP_EXTERN_C ServerComponent * CreateHandler(Server &serv, const String &name, const data::Value &dict) {
	return new TrubachComponent(serv, name, dict);
}

NS_SA_EXT_END(trubach)
