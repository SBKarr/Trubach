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

#include "Admin.h"
#include "AdminAddChannel.cc"

NS_SA_EXT_BEGIN(trubach)

class AdminImportChannelsHandler : public AdminHandlerInterface {
public:
	virtual int onRequest() override {
		UrlView v(_queryFields.getString("target"));
		if (!v.host.empty() && v.host != _request.getHostname()) {
			return HTTP_BAD_REQUEST;
		}

		if (auto file = getInputFile("content")) {
			auto fileData = file->readText();

			auto readCsvLine = [] (StringView &r, const Callback<void(size_t nfield, StringView)> &cb) {
				size_t i = 0;
				while (!r.empty() && !r.is('\r') && !r.is('\n')) {
					r.skipChars<StringView::CharGroup<CharGroupId::WhiteSpace>>();
					if (r.is('"')) {
						while (r.is('"')) {
							++ r;
							auto tmp = r.readUntil<StringView::Chars<'"'>>();
							if (r.is('"')) {
								if (r.size() > 1 && r[1] == '"') {
									cb(i, StringView(tmp.data(), tmp.size() + 1));
								} else {
									cb(i, tmp);
								}
								++ r;
							} else if (r.empty()) {
								cb(i, tmp);
							}
						}
						r.skipUntil<StringView::Chars<',', '\r', '\n'>>();
						if (r.is(',')) {
							++ r;
						}
					} else {
						auto str = r.readUntil<StringView::Chars<',', '\r', '\n'>>();
						cb(i, str);
						if (r.is(',')) {
							++ r;
						}
					}
					++ i;
				}
			};

			StringView r(fileData);
			data::Value users;
			while (!r.empty()) {
				readCsvLine(r, [&] (size_t i, StringView f) {
					if (i == 2) {
						if (f.starts_with("https://")) {
							Admin_addChannel(_component, _transaction, f);
						}
					}
				});
				r.skipChars<StringView::Chars<'\n', '\r'>>();
			}
		}

		return _request.redirectTo(v.get());
	}
};

class AdminHandlerMap : public HandlerMap {
public:
	AdminHandlerMap() {
		addHandler("AddChannel", Request::Method::Post, "/addChannel", SA_HANDLER(AdminAddChannelHandler))
				.addInputFields(mem::Vector<db::Field>({
			db::Field::Integer("section", db::Flags::Required),
			db::Field::Text("url", db::Transform::Url, db::MaxLength(1_KiB), db::Flags::Required)
		}))
				.addQueryFields(mem::Vector<db::Field>({
			db::Field::Text("target", db::Transform::Url, db::MaxLength(1_KiB))
		}));

		addHandler("UpdateChannel", Request::Method::Get, "/updateChannel/:id", SA_HANDLER(AdminUpdateChannelHandler))
				.addQueryFields(mem::Vector<db::Field>({
			db::Field::Text("target", db::Transform::Url, db::MaxLength(1_KiB))
		}));

		addHandler("ReadChannels", Request::Method::Get, "/readVideos/:id", SA_HANDLER(AdminReadVideoslHandler))
				.addQueryFields(mem::Vector<db::Field>({
			db::Field::Text("target", db::Transform::Url, db::MaxLength(1_KiB))
		}));

		addHandler("ImportChannels", Request::Method::Post, "/importChannels", SA_HANDLER(AdminImportChannelsHandler))
				.addInputFields(mem::Vector<db::Field>({
			db::Field::File("content", db::MaxFileSize(100_KiB), mem::Vector<mem::String>({"text/csv"}))
		}));

		addHandler("UpdateStats", Request::Method::Get, "/updateStats", SA_HANDLER(AdminUpdateStatsHandler));
	}
};

bool AdminHandlerInterface::isPermitted() {
	_transaction = db::Transaction::acquire(_request.storage());
	_component = _request.server().getComponent<TrubachComponent>();
	if (_component && _transaction && toInt(_transaction.getRole()) >= toInt(db::AccessRoleId::Admin)) {
		return true;
	}
	return false;
}

NS_SA_EXT_END(trubach)
