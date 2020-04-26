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

#ifndef SRC_ADMIN_ADMIN_H_
#define SRC_ADMIN_ADMIN_H_

#include "Trubach.h"
#include "Output.h"

NS_SA_EXT_BEGIN(trubach)

class AdminHandlerInterface : public HandlerMap::Handler {
public:
	AdminHandlerInterface() { }

	virtual bool isPermitted() override;

protected:
	const TrubachComponent *_component = nullptr;
	db::Transaction _transaction = nullptr;
};

NS_SA_EXT_END(trubach)

#endif /* SRC_ADMIN_ADMIN_H_ */
