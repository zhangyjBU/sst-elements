// Copyright 2016 IBM Corporation

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef C_BANKSTATEWRITE_HPP
#define C_BANKSTATEWRITE_HPP

#include <memory>
#include <list>

#include "c_BankState.hpp"
#include "c_BankCommand.hpp"

namespace SST {
namespace n_Bank {

class c_BankStateActive;
class c_BankStateWriteA;
//class c_BankStatePrecharge;
class c_BankStateRead;

class c_BankStateWrite: public c_BankState {

public:

	c_BankStateWrite(std::map<std::string, unsigned>* x_bankParams);
	~c_BankStateWrite();

	virtual void handleCommand(c_BankInfo* x_bank, c_BankCommand* x_bankCommandPtr);

	virtual void clockTic(c_BankInfo* x_bank);

	virtual void enter(c_BankInfo* x_bank,
			c_BankState* x_prevState, c_BankCommand* x_cmdPtr);

	virtual std::list<e_BankCommandType> getAllowedCommands();

	virtual bool isCommandAllowed(c_BankCommand* x_cmdPtr,
			c_BankInfo* x_bankPtr);

private:
	unsigned m_timer; // counts down to 0
	unsigned m_timerExit; // counts down to 0 during state exit

	std::list<e_BankCommandType> m_allowedCommands;
	c_BankCommand* m_receivedCommandPtr;
	c_BankCommand* m_prevCommandPtr;
	c_BankState* m_nextStatePtr;
};

}
}
#endif // C_BANKSTATEWRITE_HPP