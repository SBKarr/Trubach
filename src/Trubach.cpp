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

#include "STPqHandle.h"

NS_SA_EXT_BEGIN(trubach)

TrubachComponent::TrubachComponent(Server &serv, const String &name, const data::Value &dict)
: ServerComponent(serv, name, dict) {
	using namespace db;

	auto users = serv.addComponent(new UsersComponent(serv, "Users", dict));

	_users = users;

	exportValues(_channels, _videos, _groups, _sections, _chanstat, _chansnap, _notifiers, _timers, _messages);

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
		Field::Set("videos", _videos),
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

	_timers.define(Vector<Field>({
		Field::Integer("date", Flags::Indexed, DefaultFn([&] (const data::Value &) -> data::Value {
			return data::Value(Time::now().toMicros());
		})),
		Field::Integer("interval", Flags::Indexed, data::Value(0)),
		Field::Text("tag", MinLength(1), Flags::Indexed | Flags::Unique),
	}));

	_messages.define(Vector<Field>({
		Field::Text("tag", MinLength(1)),
		Field::Text("type", MinLength(1)),
		Field::Text("text", MinLength(1)),
		Field::Integer("object"),
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
			return subscribeAll(t);
		}
		return data::Value("Fail to perform command");
	});

	addCommand("unsubscribe-all", [this] (const StringView &str) -> data::Value {
		if (auto t = db::Transaction::acquire()) {
			return unsubscribeAll(t);
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
			task.performWithStorage([&] (const db::Transaction &t) {
				updateTimers(t);
			});
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

void TrubachComponent::onHeartbeat(Server &serv) {
	Time now = Time::now();

	auto diff = now - _lastUpdate;
	if (diff > TimeInterval::seconds(5)) {
		serv.performWithStorage([&] (db::Transaction &t) {
			t.setRole(db::AccessRoleId::System);

			StringStream stream;
			stream << "UPDATE timers SET date=" << now.toMicros() << " WHERE date+interval < " << now.toMicros()
					<< " RETURNING __oid, date, tag, interval;";

			Vector<StringView> toUpdate;
			if (auto h = dynamic_cast<db::pq::Handle *>(t.getAdapter().interface())) {
				h->performSimpleSelect(stream.weak(), [&] (db::sql::Result &res) {
					if (res.nrows() > 0) {
						for (auto it : res) {
							toUpdate.emplace_back(it.toString(2).pdup());
						}
					}
				});
			}

			for (auto &it : toUpdate) {
				onTagUpdate(t, it);
			}
		});
		_lastUpdate = now;
	}
}

UsersComponent *TrubachComponent::getUsers() const {
	return _users;
}

const db::Scheme &TrubachComponent::getChannels() const { return _channels; }
const db::Scheme &TrubachComponent::getVideos() const { return _videos; }
const db::Scheme &TrubachComponent::getGroups() const { return _groups; }
const db::Scheme &TrubachComponent::getSections() const { return _sections; }
const db::Scheme &TrubachComponent::getChanstat() const { return _chanstat; }
const db::Scheme &TrubachComponent::getChansnap() const { return _chansnap; }
const db::Scheme &TrubachComponent::getNotifiers() const { return _notifiers; }
const db::Scheme &TrubachComponent::getMessages() const { return _messages; }

void TrubachComponent::onTagUpdate(db::Transaction &t, StringView tag) const {
	if (tag == "subscriptions") {
#if RELEASE
		Task::perform(_server, [&] (Task &task) {
			task.addExecuteFn([this] (const Task &task) -> bool {
				task.performWithStorage([&] (const db::Transaction &t) {
					subscribeAll(t);
				});
				return true;
			});
		});
#endif
	} else {
		std::cout << tag << "\n";
	}
}

data::Value TrubachComponent::subscribeAll(const db::Transaction &t) const {
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

data::Value TrubachComponent::unsubscribeAll(const db::Transaction &t) const {
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

bool TrubachComponent::subscribeChannel(const data::Value &ch) const {
	if (ch.getInteger("subscribed") <= int64_t((Time::now() - TimeInterval::seconds(1200)).toMicros())) {
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

void TrubachComponent::updateTimers(const db::Transaction &t) const {
	_timers.create(t, data::Value({
		pair("tag", data::Value("subscriptions")),
		pair("interval", data::Value(TimeInterval::seconds(60 * 10)))
	}), db::Conflict::DoNothing);
}

SP_EXTERN_C ServerComponent * CreateHandler(Server &serv, const String &name, const data::Value &dict) {
	return new TrubachComponent(serv, name, dict);
}

NS_SA_EXT_END(trubach)
