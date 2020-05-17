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

#ifndef SRC_TRUBACH_H_
#define SRC_TRUBACH_H_

#include "RequestHandler.h"
#include "ServerComponent.h"
#include "Task.h"
#include "Config.h"
#include "Networking.h"

NS_SA_EXT_BEGIN(trubach)

static constexpr storage::AccessRoleId NonAuthorizedUser = storage::AccessRoleId::Nobody;
static constexpr storage::AccessRoleId AuthorizedUser = storage::AccessRoleId::Authorized;

class UsersComponent;

enum class NotificationFormat {
	Plain,
	Markdown,
	Html,
};

void sendTgMessage(const data::Value &note, const StringView &data, NotificationFormat fmt, const data::Value &extra = data::Value());
void updateTgIntegration();

class TrubachComponent : public ServerComponent {
public:
	TrubachComponent(Server &serv, const String &name, const data::Value &dict);
	virtual ~TrubachComponent() { }

	virtual void onChildInit(Server &) override;
	virtual void onStorageTransaction(db::Transaction &t) override;
	virtual void onHeartbeat(Server &) override;

	UsersComponent *getUsers() const;

	const Scheme &getChannels() const;
	const Scheme &getVideos() const;
	const Scheme &getGroups() const;
	const Scheme &getSections() const;

	const Scheme &getChanstat() const;
	const Scheme &getChansnap() const;
	const Scheme &getNotifiers() const;
	const Scheme &getMessages() const;

protected:
	void onTagUpdate(db::Transaction &, StringView) const;

	data::Value subscribeAll(const db::Transaction &) const;
	data::Value unsubscribeAll(const db::Transaction &) const;

	bool subscribeChannel(const data::Value &) const;
	bool unsubscribeChannel(const data::Value &) const;

	void updateTimers(const db::Transaction &) const;

	Time _lastUpdate = Time::now();

	Scheme _channels = Scheme("channels");
	Scheme _videos = Scheme("videos");
	Scheme _groups = Scheme("groups");
	Scheme _sections = Scheme("sections");

	Scheme _chanstat = Scheme("chanstat", Scheme::Detouched);
	Scheme _chansnap = Scheme("chansnap", Scheme::Detouched);

	Scheme _notifiers = Scheme("notifiers");
	Scheme _timers = Scheme("timers");
	Scheme _messages = Scheme("messages");

	UsersComponent *_users = nullptr;
};

NS_SA_EXT_END(trubach)

#endif
