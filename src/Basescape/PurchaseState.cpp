/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "PurchaseState.h"
#include "ItemLocationsState.h"
#include <sstream>
#include <climits>
#include <iomanip>
#include <algorithm>
#include <locale>
#include "../fmath.h"
#include "../Engine/Game.h"
#include "../Mod/Mod.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Timer.h"
#include "../Engine/Options.h"
#include "../Engine/Unicode.h"
#include "../Interface/TextButton.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/TextEdit.h"
#include "../Interface/ComboBox.h"
#include "../Interface/TextList.h"
#include "../Savegame/SavedGame.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/Base.h"
#include "../Engine/Action.h"
#include "../Savegame/Craft.h"
#include "../Savegame/ItemContainer.h"
#include "../Menu/ErrorMessageState.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Ufopaedia/Ufopaedia.h"
#include "../Battlescape/CannotReequipState.h"
#include "../Savegame/Country.h"
#include "../Mod/RuleCountry.h"

namespace OpenXcom
{

/**
 * @brief Combines any number of functions into a function that returns true if all of them are true.
 * Short circuits as well.
 * @tparam ...Functions type of the function to combine.
 * @param ...funcs the functions to combine.
 * @return a new function that is true only if all predicates are satisfied.
*/
template <typename... Functions>
inline constexpr auto allOf(Functions... funcs)
{
	return [=](const auto& x)
	{
		return (... && funcs(x));
	};
}

/**
 * Initializes all the elements in the Purchase/Hire screen.
 * @param game Pointer to the core game.
 * @param base Pointer to the base to get info from.
 */
PurchaseState::PurchaseState(Base *base, CannotReequipState *parent) : _base(base), _parent(parent), _sel(0), _total(0), _pQty(0), _iQty(0.0), _ammoColor(0)
{
	_autoBuyDone = false;
	if (_parent)
	{
		for (auto& i : _parent->getMissingItems())
		{
			if (i.qty > 0)
			{
				auto* rule = _game->getMod()->getItem(i.item);
				if (rule)
				{
					_missingItemsMap[rule] = i.qty;
				}
			}
		}
	}

	// Create objects
	_window = new Window(this, 320, 200, 0, 0);
	_btnQuickSearch = new TextEdit(this, 48, 9, 10, 13);
	_btnOk = new TextButton(148, 16, 8, 176);
	_btnCancel = new TextButton(148, 16, 164, 176);
	_txtTitle = new Text(310, 17, 5, 8);
	_txtFunds = new Text(150, 9, 10, 24);
	_txtPurchases = new Text(150, 9, 160, 24);
	_txtSpaceUsed = new Text(150, 9, 160, 34);
	_txtCost = new Text(102, 9, 152, 44);
	_txtQuantity = new Text(60, 9, 256, 44);
	_cbxCategory = new ComboBox(this, 120, 16, 10, 36);
	_lstItems = new TextList(287, 120, 8, 54);

	touchComponentsCreate(_txtTitle);

	// Set palette
	setInterface("buyMenu");

	_ammoColor = _game->getMod()->getInterface("buyMenu")->getElement("ammoColor")->color;

	add(_window, "window", "buyMenu");
	add(_btnQuickSearch, "button", "buyMenu");
	add(_btnOk, "button", "buyMenu");
	add(_btnCancel, "button", "buyMenu");
	add(_txtTitle, "text", "buyMenu");
	add(_txtFunds, "text", "buyMenu");
	add(_txtPurchases, "text", "buyMenu");
	add(_txtSpaceUsed, "text", "buyMenu");
	add(_txtCost, "text", "buyMenu");
	add(_txtQuantity, "text", "buyMenu");
	add(_lstItems, "list", "buyMenu");
	add(_cbxCategory, "text", "buyMenu");

	touchComponentsAdd("button2", "buyMenu", _window);

	centerAllSurfaces();

	// Set up objects
	setWindowBackground(_window, "buyMenu");

	touchComponentsConfigure();

	_btnOk->setText(tr("STR_OK"));
	_btnOk->onMouseClick((ActionHandler)&PurchaseState::btnOkClick);
	_btnOk->onKeyboardPress((ActionHandler)&PurchaseState::btnOkClick, Options::keyOk);

	_btnCancel->setText(tr("STR_CANCEL"));
	_btnCancel->onMouseClick((ActionHandler)&PurchaseState::btnCancelClick);
	_btnCancel->onKeyboardPress((ActionHandler)&PurchaseState::btnCancelClick, Options::keyCancel);

	_txtTitle->setBig();
	_txtTitle->setAlign(ALIGN_CENTER);
	_txtTitle->setText(tr("STR_PURCHASE_HIRE_PERSONNEL"));

	_txtFunds->setText(tr("STR_CURRENT_FUNDS").arg(Unicode::formatFunding(_game->getSavedGame()->getFunds())));

	_txtPurchases->setText(tr("STR_COST_OF_PURCHASES").arg(Unicode::formatFunding(_total)));

	_txtSpaceUsed->setVisible(Options::storageLimitsEnforced);
	std::ostringstream ss;
	ss << _base->getUsedStores() << ":" << _base->getAvailableStores();
	_txtSpaceUsed->setText(tr("STR_SPACE_USED").arg(ss.str()));

	_txtCost->setText(tr("STR_COST_PER_UNIT_UC"));

	_txtQuantity->setText(tr("STR_QUANTITY_UC"));

	_lstItems->setArrowColumn(227, ARROW_VERTICAL);
//	_lstItems->setColumns(4, 150, 55, 50, 32);
	_lstItems->setColumns(4, 140, 40, 40, 50, 17);
	_lstItems->setAlign(ALIGN_RIGHT, 1);
	_lstItems->setAlign(ALIGN_RIGHT, 2);
	_lstItems->setAlign(ALIGN_RIGHT, 3);
	_lstItems->setSelectable(true);
	_lstItems->setBackground(_window);
	_lstItems->setMargin(2);
	_lstItems->onLeftArrowPress((ActionHandler)&PurchaseState::lstItemsLeftArrowPress);
	_lstItems->onLeftArrowRelease((ActionHandler)&PurchaseState::lstItemsLeftArrowRelease);
	_lstItems->onLeftArrowClick((ActionHandler)&PurchaseState::lstItemsLeftArrowClick);
	_lstItems->onRightArrowPress((ActionHandler)&PurchaseState::lstItemsRightArrowPress);
	_lstItems->onRightArrowRelease((ActionHandler)&PurchaseState::lstItemsRightArrowRelease);
	_lstItems->onRightArrowClick((ActionHandler)&PurchaseState::lstItemsRightArrowClick);
	_lstItems->onMousePress((ActionHandler)&PurchaseState::lstItemsMousePress);

	_cats.push_back("STR_ALL_ITEMS");
	_cats.push_back("STR_FILTER_HIDDEN");
	_cats.push_back("STR_FILTER_EQUIPPED");
	if (!_missingItemsMap.empty())
	{
		_cats.push_back("STR_FILTER_MISSING");
	}

	RuleBaseFacilityFunctions providedBaseFunc = _base->getProvidedBaseFunc({});

	// setup the filter methods to be used in various situations (craft, soldiers, items).
	constexpr auto costIsNotZero = [](const auto* rule)
	{
		return rule->getBuyCost() != 0;
	};
	constexpr auto requirementsAreResearched = [](const auto* rule)
	{
		return _game->getSavedGame()->isResearched(rule->getRequirements());
	};
	constexpr auto buyRequirementsAreResearched = [](const auto* rule)
	{
		return _game->getSavedGame()->isResearched(rule->getBuyRequirements());
	};
	auto necessaryBaseFunctionsPresent = [&providedBaseFunc](const auto* rule)
	{
		return (~providedBaseFunc & rule->getRequiresBuyBaseFunc()).none();
	};
	constexpr auto requiredCountryAllied = [](const auto* rule)
	{
		if (!rule->getRequiresBuyCountry().empty())
		{
			for (const auto* country : *_game->getSavedGame()->getCountries())
			{
				if (country->getPact() && country->getRules()->getType() == rule->getRequiresBuyCountry())
				{
					return false;
				}
			}
		}
		return true;
	};

	auto craftAndSoldierFilter = allOf(costIsNotZero, requirementsAreResearched, necessaryBaseFunctionsPresent, requiredCountryAllied);
	auto itemFilter = allOf(costIsNotZero, requirementsAreResearched, buyRequirementsAreResearched, necessaryBaseFunctionsPresent, requiredCountryAllied);

	for (auto& soldierType : _game->getMod()->getSoldiersList())
	{
		RuleSoldier *rule = _game->getMod()->getSoldier(soldierType);
		if (craftAndSoldierFilter(rule))
		{
			TransferRow row = { TRANSFER_SOLDIER, rule, tr(rule->getType()), rule->getBuyCost(), _base->getSoldierCountAndSalary(rule->getType()).first, 0, 0, -4, 0, 0, 0 };
			_items.push_back(row);
			std::string cat = getCategory(_items.size() - 1);
			if (std::find(_cats.begin(), _cats.end(), cat) == _cats.end())
			{
				_cats.push_back(cat);
			}
		}
	}
	if ((_game->getMod()->getHireScientistsUnlockResearch().empty() || _game->getSavedGame()->isResearched(_game->getMod()->getHireScientistsUnlockResearch(), true))
		&& (~providedBaseFunc & _game->getMod()->getHireScientistsRequiresBaseFunc()).none())
	{
		TransferRow row = { TRANSFER_SCIENTIST, 0, tr("STR_SCIENTIST"), _game->getMod()->getHireScientistCost(), _base->getTotalScientists(), 0, 0, -3, 0, 0, 0 };
		_items.push_back(row);
		std::string cat = getCategory(_items.size() - 1);
		if (std::find(_cats.begin(), _cats.end(), cat) == _cats.end())
		{
			_cats.push_back(cat);
		}
	}
	if ((_game->getMod()->getHireEngineersUnlockResearch().empty() || _game->getSavedGame()->isResearched(_game->getMod()->getHireEngineersUnlockResearch(), true))
		&& (~providedBaseFunc & _game->getMod()->getHireEngineersRequiresBaseFunc()).none())
	{
		TransferRow row = { TRANSFER_ENGINEER, 0, tr("STR_ENGINEER"), _game->getMod()->getHireEngineerCost(), _base->getTotalEngineers(), 0, 0, -2, 0, 0, 0 };
		_items.push_back(row);
		std::string cat = getCategory(_items.size() - 1);
		if (std::find(_cats.begin(), _cats.end(), cat) == _cats.end())
		{
			_cats.push_back(cat);
		}
	}
	for (auto& craftType : _game->getMod()->getCraftsList())
	{
		RuleCraft *rule = _game->getMod()->getCraft(craftType);
		if (craftAndSoldierFilter(rule))
		{
			TransferRow row = { TRANSFER_CRAFT, rule, tr(rule->getType()), rule->getBuyCost(), _base->getCraftCount(rule), 0, 0, -1, 0, 0, 0 };
			_items.push_back(row);
			std::string cat = getCategory(_items.size() - 1);
			if (std::find(_cats.begin(), _cats.end(), cat) == _cats.end())
			{
				_cats.push_back(cat);
			}
		}
	}
	for (auto& itemType : _game->getMod()->getItemsList())
	{
		RuleItem *rule = _game->getMod()->getItem(itemType);
		if (itemFilter(rule))
		{
			TransferRow row = { TRANSFER_ITEM, rule, tr(rule->getType()), rule->getBuyCostAdjusted(_base, _game->getSavedGame()), _base->getStorageItems()->getItem(rule), 0, 0, rule->getListOrder(), 0, 0, 0 };
			_items.push_back(row);
			std::string cat = getCategory(_items.size() - 1);
			if (std::find(_cats.begin(), _cats.end(), cat) == _cats.end())
			{
				_cats.push_back(cat);
			}
		}
	}

	_vanillaCategories = _cats.size();
	if (_game->getMod()->getDisplayCustomCategories() > 0)
	{
		bool hasUnassigned = false;

		// first find all relevant item categories
		std::vector<std::string> tempCats;
		for (const auto& transferRow : _items)
		{
			if (transferRow.type == TRANSFER_ITEM)
			{
				RuleItem *rule = (RuleItem*)(transferRow.rule);
				if (rule->getCategories().empty())
				{
					hasUnassigned = true;
				}
				for (auto& itemCategoryName : rule->getCategories())
				{
					if (std::find(tempCats.begin(), tempCats.end(), itemCategoryName) == tempCats.end())
					{
						tempCats.push_back(itemCategoryName);
					}
				}
			}
		}
		// then use them nicely in order
		if (_game->getMod()->getDisplayCustomCategories() == 1)
		{
			_cats.clear();
			_cats.push_back("STR_ALL_ITEMS");
			_cats.push_back("STR_FILTER_HIDDEN");
			_cats.push_back("STR_FILTER_EQUIPPED");
			if (!_missingItemsMap.empty())
			{
				_cats.push_back("STR_FILTER_MISSING");
			}
			_vanillaCategories = _cats.size();
		}
		for (auto& categoryName : _game->getMod()->getItemCategoriesList())
		{
			if (std::find(tempCats.begin(), tempCats.end(), categoryName) != tempCats.end())
			{
				_cats.push_back(categoryName);
			}
		}
		if (hasUnassigned)
		{
			_cats.push_back("STR_UNASSIGNED");
		}
	}

	_cbxCategory->setOptions(_cats, true);
	if (!_missingItemsMap.empty())
	{
		_cbxCategory->setSelected(3); // STR_FILTER_MISSING
	}
	_cbxCategory->onChange((ActionHandler)&PurchaseState::cbxCategoryChange);

	_btnQuickSearch->setText(""); // redraw
	_btnQuickSearch->onEnter((ActionHandler)&PurchaseState::btnQuickSearchApply);
	_btnQuickSearch->setVisible(Options::oxceQuickSearchButton);

	_btnOk->onKeyboardRelease((ActionHandler)&PurchaseState::btnQuickSearchToggle, Options::keyToggleQuickSearch);

	updateList();

	_autoBuyDone = true;
	if (!_missingItemsMap.empty())
	{
		_txtPurchases->setText(tr("STR_COST_OF_PURCHASES").arg(Unicode::formatFunding(_total)));
		std::ostringstream ss5;
		ss5 << _base->getUsedStores();
		if (std::abs(_iQty) > 0.05)
		{
			ss5 << "(";
			if (_iQty > 0.05)
				ss5 << "+";
			ss5 << std::fixed << std::setprecision(1) << _iQty << ")";
		}
		ss5 << ":" << _base->getAvailableStores();
		_txtSpaceUsed->setText(tr("STR_SPACE_USED").arg(ss5.str()));
	}

	_timerInc = new Timer(250);
	_timerInc->onTimer((StateHandler)&PurchaseState::increase);
	_timerDec = new Timer(250);
	_timerDec->onTimer((StateHandler)&PurchaseState::decrease);
}

/**
 *
 */
PurchaseState::~PurchaseState()
{
	delete _timerInc;
	delete _timerDec;
}

/**
 * Resets stuff when coming back from other screens.
 */
void PurchaseState::init()
{
	State::init();

	touchComponentsRefresh();
}

/**
 * Runs the arrow timers.
 */
void PurchaseState::think()
{
	State::think();

	_timerInc->think(this, 0);
	_timerDec->think(this, 0);
}

/**
 * Determines the category a row item belongs in.
 * @param sel Selected row.
 * @returns Item category.
 */
std::string PurchaseState::getCategory(int sel) const
{
	RuleItem *rule = 0;
	switch (_items[sel].type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
		return "STR_PERSONNEL";
	case TRANSFER_CRAFT:
		return "STR_CRAFT_ARMAMENT";
	case TRANSFER_ITEM:
		rule = (RuleItem*)_items[sel].rule;
		if (rule->getBattleType() == BT_CORPSE || rule->isAlien())
		{
			if (rule->getVehicleUnit())
				return "STR_PERSONNEL"; // OXCE: critters fighting for us
			if (rule->isAlien())
				return "STR_PRISONERS"; // OXCE: live aliens
			return "STR_ALIENS";
		}
		if (rule->getBattleType() == BT_NONE)
		{
			if (_game->getMod()->isCraftWeaponStorageItem(rule))
				return "STR_CRAFT_ARMAMENT";
			if (_game->getMod()->isArmorStorageItem(rule))
				return "STR_ARMORS"; // OXCE: armors
			return "STR_COMPONENTS";
		}
		return "STR_EQUIPMENT";
	}
	return "STR_ALL_ITEMS";
}

/**
 * Determines if a row item belongs to a given category.
 * @param sel Selected row.
 * @param cat Category.
 * @returns True if row item belongs to given category, otherwise False.
 */
bool PurchaseState::belongsToCategory(int sel, const std::string &cat) const
{
	switch (_items[sel].type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
	case TRANSFER_CRAFT:
		return false;
	case TRANSFER_ITEM:
		RuleItem *rule = (RuleItem*)_items[sel].rule;
		return rule->belongsToCategory(cat);
	}
	return false;
}

/**
 * Determines if a row item is supposed to be hidden
 * @param sel Selected row.
 * @param cat Category.
 * @returns True if row item is hidden
 */
bool PurchaseState::isHidden(int sel) const
{
	std::string itemName;
	bool isCraft = false;

	switch (_items[sel].type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
		return false;
	case TRANSFER_CRAFT:
		isCraft = true; // fall-through
	case TRANSFER_ITEM:
		if (isCraft)
		{
			RuleCraft *rule = (RuleCraft*)_items[sel].rule;
			if (rule != 0)
			{
				itemName = rule->getType();
			}
		}
		else
		{
			RuleItem *rule = (RuleItem*)_items[sel].rule;
			if (rule != 0)
			{
				itemName = rule->getType();
			}
		}
		if (!itemName.empty())
		{
			auto& hiddenMap = _game->getSavedGame()->getHiddenPurchaseItems();
			auto iter = hiddenMap.find(itemName);
			if (iter != hiddenMap.end())
			{
				return iter->second;
			}
			else
			{
				// not found = not hidden
				return false;
			}
		}
	}

	return false;
}

/**
 * Determines if a row item corresponds to equipped items
 * @param sel Selected row.
 * @returns True if row item is considered equipped
 */
bool PurchaseState::isEquipped(int sel) const
{
	switch (_items[sel].type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
	case TRANSFER_CRAFT:
		return false;
	case TRANSFER_ITEM:
		RuleItem* rule = (RuleItem*)_items[sel].rule;
		if (rule)
		{
			// iterate all craft, also craft which are currently not at the base
			for (auto* xcraft : *_base->getCrafts())
			{
				if (xcraft->getItems()->getItem(rule) > 0)
					return true;
			}
		}
	}

	return false;
}

/**
 * Determines if a row item is in the map of missing items.
 * @param sel Selected row.
 * @returns Number of missing items that can be bought. -1 if not missing.
 */
int PurchaseState::getMissingQty(int sel) const
{
	switch (_items[sel].type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
	case TRANSFER_CRAFT:
		return -1;
	case TRANSFER_ITEM:
		RuleItem* rule = (RuleItem*)_items[sel].rule;
		if (rule)
		{
			auto iter = _missingItemsMap.find(rule);
			if (iter != _missingItemsMap.end())
			{
				if (rule->getMonthlyBuyLimit() > 0)
				{
					auto& itemPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
					int maxByLimit = std::max(0, rule->getMonthlyBuyLimit() - itemPurchaseLimitLog[rule->getType()]);
					return std::min(maxByLimit, iter->second);
				}

				return iter->second;
			}
			else
			{
				// not found = not missing
				return -1;
			}
		}
	}

	return -1;
}

/**
* Quick search toggle.
* @param action Pointer to an action.
*/
void PurchaseState::btnQuickSearchToggle(Action *action)
{
	if (_btnQuickSearch->getVisible())
	{
		_btnQuickSearch->setText("");
		_btnQuickSearch->setVisible(false);
		btnQuickSearchApply(action);
	}
	else
	{
		_btnQuickSearch->setVisible(true);
		_btnQuickSearch->setFocus(true);
	}
}

/**
* Quick search.
* @param action Pointer to an action.
*/
void PurchaseState::btnQuickSearchApply(Action *)
{
	updateList();
}

/**
 * Filters the current list of items.
 */
void PurchaseState::updateList()
{
	std::string searchString = _btnQuickSearch->getText();
	Unicode::upperCase(searchString);

	_lstItems->clearList();
	_rows.clear();

	size_t selCategory = _cbxCategory->getSelected();
	const std::string selectedCategory = _cats[selCategory];
	bool categoryFilterEnabled = (selectedCategory != "STR_ALL_ITEMS");
	bool categoryUnassigned = (selectedCategory == "STR_UNASSIGNED");
	bool categoryHidden = (selectedCategory == "STR_FILTER_HIDDEN");
	bool categoryEquipped = (selectedCategory == "STR_FILTER_EQUIPPED");
	bool categoryMissing = (selectedCategory == "STR_FILTER_MISSING");

	for (size_t i = 0; i < _items.size(); ++i)
	{
		// filter
		if (categoryMissing)
		{
			int missingQty = getMissingQty(i);
			if (missingQty > -1)
			{
				if (!_autoBuyDone)
				{
					RuleItem* rule = (RuleItem*)_items[i].rule;
					if (rule->isAlien())
					{
						// don't buy automatically
					}
					else
					{
						_items[i].amount += missingQty; // buy automatically

						_iQty  += missingQty * rule->getSize(); // update total size
						_total += missingQty * _items[i].cost;  // update total cost
					}
				}
			}
			else
			{
				continue;
			}
		}
		else if (categoryEquipped)
		{
			// Note: showing also hidden items (if they are equipped)
			if (!isEquipped(i))
			{
				continue;
			}
		}
		else
		{
			bool hidden = isHidden(i);
			if (categoryHidden)
			{
				if (!hidden)
				{
					continue;
				}
			}
			else if (hidden)
			{
				continue;
			}
			else if (selCategory >= _vanillaCategories)
			{
				if (categoryUnassigned && _items[i].type == TRANSFER_ITEM)
				{
					RuleItem* rule = (RuleItem*)_items[i].rule;
					if (!rule->getCategories().empty())
					{
						continue;
					}
				}
				else if (categoryFilterEnabled && !belongsToCategory(i, selectedCategory))
				{
					continue;
				}
			}
			else
			{
				if (categoryFilterEnabled && selectedCategory != getCategory(i))
				{
					continue;
				}
			}
		}

		// quick search
		if (!searchString.empty())
		{
			std::string projectName = _items[i].name;
			Unicode::upperCase(projectName);
			if (projectName.find(searchString) == std::string::npos)
			{
				continue;
			}
		}

		std::string name = _items[i].name;
		bool ammo = false;
		if (_items[i].type == TRANSFER_ITEM)
		{
			RuleItem *rule = (RuleItem*)_items[i].rule;
			ammo = (rule->getBattleType() == BT_AMMO || (rule->getBattleType() == BT_NONE && rule->getClipSize() > 0));
			if (ammo)
			{
				name.insert(0, "  ");
			}
		}
		std::ostringstream ssQty, ssAmount;
		ssQty << _items[i].qtySrc;
		ssAmount << _items[i].amount;
		_lstItems->addRow(4, name.c_str(), Unicode::formatFunding(_items[i].cost).c_str(), ssQty.str().c_str(), ssAmount.str().c_str());
		_rows.push_back(i);
		if (_items[i].amount > 0)
		{
			_lstItems->setRowColor(_rows.size() - 1, _lstItems->getSecondaryColor());
		}
		else if (ammo)
		{
			_lstItems->setRowColor(_rows.size() - 1, _ammoColor);
		}
	}
}

/**
 * Purchases the selected items.
 * @param action Pointer to an action.
 */
void PurchaseState::btnOkClick(Action *)
{
	if (!_missingItemsMap.empty())
	{
		std::string errorMessage;
		if (_total > _game->getSavedGame()->getFunds())
		{
			errorMessage = tr("STR_NOT_ENOUGH_MONEY");
		}
		if (_base->storesOverfull(_iQty))
		{
			errorMessage = tr("STR_NOT_ENOUGH_STORE_SPACE");
		}
		if (!errorMessage.empty())
		{
			RuleInterface* menuInterface = _game->getMod()->getInterface("buyMenu");
			_game->pushState(new ErrorMessageState(errorMessage, _palette, menuInterface->getElement("errorMessage")->color, "BACK13.SCR", menuInterface->getElement("errorPalette")->color));
			return;
		}
	}

	_game->getSavedGame()->setFunds(_game->getSavedGame()->getFunds() - _total);
	for (const auto& transferRow : _items)
	{
		if (transferRow.amount > 0)
		{
			Transfer *t = 0;
			switch (transferRow.type)
			{
			case TRANSFER_SOLDIER:
				for (int s = 0; s < transferRow.amount; s++)
				{
					RuleSoldier *rule = (RuleSoldier*)transferRow.rule;
					if (rule->getMonthlyBuyLimit() > 0)
					{
						// remember the hire for the limit check
						auto& soldierHireLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
						soldierHireLimitLog[rule->getType()] += 1;
					}
					int time = rule->getTransferTime();
					if (time == 0)
						time = _game->getMod()->getPersonnelTime();
					t = new Transfer(time);
					int nationality = _game->getSavedGame()->selectSoldierNationalityByLocation(_game->getMod(), rule, _base);
					Soldier* soldier = _game->getMod()->genSoldier(_game->getSavedGame(), rule, nationality);
					if (!rule->getSpawnedSoldierTemplate().yaml.empty())
					{
						YAML::YamlRootNodeReader reader(rule->getSpawnedSoldierTemplate(), "(spawned soldier template)");
						int nationalityOrig = soldier->getNationality();
						soldier->load(reader.toBase(), _game->getMod(), _game->getSavedGame(), _game->getMod()->getScriptGlobal(), true); // load from soldier template
						if (soldier->getNationality() != nationalityOrig)
						{
							soldier->genName();
						}
					}
					t->setSoldier(soldier);
					_base->getTransfers()->push_back(t);
				}
				break;
			case TRANSFER_SCIENTIST:
				t = new Transfer(_game->getMod()->getPersonnelTime());
				t->setScientists(transferRow.amount);
				_base->getTransfers()->push_back(t);
				break;
			case TRANSFER_ENGINEER:
				t = new Transfer(_game->getMod()->getPersonnelTime());
				t->setEngineers(transferRow.amount);
				_base->getTransfers()->push_back(t);
				break;
			case TRANSFER_CRAFT:
				for (int c = 0; c < transferRow.amount; c++)
				{
					RuleCraft *rule = (RuleCraft*)transferRow.rule;
					if (rule->getMonthlyBuyLimit() > 0)
					{
						// remember the purchase for the limit check
						auto& craftPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
						craftPurchaseLimitLog[rule->getType()] += 1;
					}
					t = new Transfer(rule->getTransferTime());
					Craft *craft = new Craft(rule, _base, _game->getSavedGame()->getId(rule->getType()));
					craft->initFixedWeapons(_game->getMod());
					craft->setStatus("STR_REFUELLING");
					t->setCraft(craft);
					_base->getTransfers()->push_back(t);
				}
				break;
			case TRANSFER_ITEM:
				{
					RuleItem *rule = (RuleItem*)transferRow.rule;
					if (rule->getMonthlyBuyLimit() > 0)
					{
						// remember the purchase for the limit check
						auto& itemPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
						itemPurchaseLimitLog[rule->getType()] += transferRow.amount;
					}
					t = new Transfer(rule->getTransferTime());
					t->setItems(rule, transferRow.amount);
					_base->getTransfers()->push_back(t);
					if (_parent && !_missingItemsMap.empty() && _missingItemsMap.find(rule) != _missingItemsMap.end())
					{
						// remember the decreased amount for next buy
						_parent->decreaseMissingItemCount(rule, transferRow.amount);
					}
				}
				break;
			}
		}
	}
	_game->popState();
}

/**
 * Returns to the previous screen.
 * @param action Pointer to an action.
 */
void PurchaseState::btnCancelClick(Action *)
{
	_game->popState();
}

/**
 * Starts increasing the item.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsLeftArrowPress(Action *action)
{
	_sel = _lstItems->getSelectedRow();
	if (_game->isLeftClick(action, true) && !_timerInc->isRunning()) _timerInc->start();
}

/**
 * Stops increasing the item.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsLeftArrowRelease(Action *action)
{
	if (_game->isLeftClick(action, true))
	{
		_timerInc->stop();
	}
}

/**
 * Increases the item by one on left-click,
 * to max on right-click.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsLeftArrowClick(Action *action)
{
	if (_game->isRightClick(action, true)) increaseByValue(INT_MAX);
	if (_game->isLeftClick(action, true))
	{
		increaseByValue(_game->getScrollStep());
		_timerInc->setInterval(250);
		_timerDec->setInterval(250);
	}
}

/**
 * Starts decreasing the item.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsRightArrowPress(Action *action)
{
	_sel = _lstItems->getSelectedRow();
	if (_game->isLeftClick(action, true) && !_timerDec->isRunning()) _timerDec->start();
}

/**
 * Stops decreasing the item.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsRightArrowRelease(Action *action)
{
	if (_game->isLeftClick(action, true))
	{
		_timerDec->stop();
	}
}

/**
 * Decreases the item by one on left-click,
 * to 0 on right-click.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsRightArrowClick(Action *action)
{
	if (_game->isRightClick(action, true)) decreaseByValue(INT_MAX);
	if (_game->isLeftClick(action, true))
	{
		decreaseByValue(_game->getScrollStep());
		_timerInc->setInterval(250);
		_timerDec->setInterval(250);
	}
}

/**
 * Handles the mouse-wheels on the arrow-buttons.
 * @param action Pointer to an action.
 */
void PurchaseState::lstItemsMousePress(Action *action)
{
	_sel = _lstItems->getSelectedRow();
	if (action->getDetails()->button.button == SDL_BUTTON_WHEELUP)
	{
		_timerInc->stop();
		_timerDec->stop();
		if (action->getAbsoluteXMouse() >= _lstItems->getArrowsLeftEdge() &&
			action->getAbsoluteXMouse() <= _lstItems->getArrowsRightEdge())
		{
			increaseByValue(Options::changeValueByMouseWheel);
		}
	}
	else if (action->getDetails()->button.button == SDL_BUTTON_WHEELDOWN)
	{
		_timerInc->stop();
		_timerDec->stop();
		if (action->getAbsoluteXMouse() >= _lstItems->getArrowsLeftEdge() &&
			action->getAbsoluteXMouse() <= _lstItems->getArrowsRightEdge())
		{
			decreaseByValue(Options::changeValueByMouseWheel);
		}
	}
	else if (_game->isMiddleClick(action, true))
	{
		if (getRow().type == TRANSFER_ITEM)
		{
			RuleItem *rule = (RuleItem*)getRow().rule;
			if (rule != 0)
			{
				std::string articleId = rule->getUfopediaType();
				Ufopaedia::openArticle(_game, articleId);
			}
		}
		else if (getRow().type == TRANSFER_CRAFT)
		{
			RuleCraft *rule = (RuleCraft*)getRow().rule;
			if (rule != 0)
			{
				std::string articleId = rule->getType();
				Ufopaedia::openArticle(_game, articleId);
			}
		}
	}
	else if (_game->isRightClick(action, true))
	{
		if (action->getAbsoluteXMouse() >= _lstItems->getArrowsLeftEdge() &&
			action->getAbsoluteXMouse() <= _lstItems->getArrowsRightEdge())
		{
			return;
		}
		std::string itemName;
		if (getRow().type == TRANSFER_ITEM)
		{
			RuleItem *rule = (RuleItem*)getRow().rule;
			if (rule != 0)
			{
				if (_game->isCtrlPressed(true))
				{
					_game->pushState(new ItemLocationsState(rule));
				}
				else
				{
					itemName = rule->getType();
				}
			}
		}
		else if (getRow().type == TRANSFER_CRAFT)
		{
			RuleCraft *rule = (RuleCraft*)getRow().rule;
			if (rule != 0)
			{
				itemName = rule->getType();
			}
		}
		if (!itemName.empty())
		{
			auto& hiddenMap = _game->getSavedGame()->getHiddenPurchaseItems();
			auto iter = hiddenMap.find(itemName);
			if (iter != hiddenMap.end())
			{
				// found => flip it
				_game->getSavedGame()->setHiddenPurchaseItemsStatus(itemName, !iter->second);
			}
			else
			{
				// not found = not hidden yet => hide it
				_game->getSavedGame()->setHiddenPurchaseItemsStatus(itemName, true);
			}

			// update screen
			size_t scrollPos = _lstItems->getScroll();
			updateList();
			_lstItems->scrollTo(scrollPos);
		}
	}
}

/**
 * Increases the quantity of the selected item to buy by one.
 */
void PurchaseState::increase()
{
	_timerDec->setInterval(50);
	_timerInc->setInterval(50);
	increaseByValue(_game->getScrollStep());
}

/**
 * Increases the quantity of the selected item to buy by "change".
 * @param change How much we want to add.
 */
void PurchaseState::increaseByValue(int change)
{
	if (0 >= change) return;
	std::string errorMessage;

	if (_total + getRow().cost > _game->getSavedGame()->getFunds())
	{
		errorMessage = tr("STR_NOT_ENOUGH_MONEY");
	}
	else
	{
		RuleItem *rule = nullptr;
		RuleSoldier* ruleS = nullptr;
		RuleCraft* ruleC = nullptr;
		switch (getRow().type)
		{
		case TRANSFER_SOLDIER:
			ruleS = (RuleSoldier*)getRow().rule;
			if (ruleS->getMonthlyBuyLimit() > 0)
			{
				auto& soldierHireLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
				int maxByLimit = std::max(0, ruleS->getMonthlyBuyLimit() - soldierHireLimitLog[ruleS->getType()] - getRow().amount);
				if (maxByLimit <= 0)
				{
					errorMessage = tr("STR_MONTHLY_SOLDIER_HIRING_LIMIT_EXCEEDED");
				}
			}
			// fall-through
		case TRANSFER_SCIENTIST:
		case TRANSFER_ENGINEER:
			if (_pQty + 1 > _base->getAvailableQuarters() - _base->getUsedQuarters())
			{
				errorMessage = tr("STR_NOT_ENOUGH_LIVING_SPACE");
			}
			break;
		case TRANSFER_CRAFT:
		{	
			ruleC = (RuleCraft*)getRow().rule;
			int p = ruleC->getHangarType();
			if (_tCQty[p] + 1 > _base->getAvailableHangars(p) - _base->getUsedHangars(p))	
			{
				errorMessage = tr("STR_NO_FREE_HANGARS_FOR_PURCHASE");
			}
			else if (ruleC->getMonthlyBuyLimit() > 0)
			{
				auto& craftPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
				int maxByLimit = std::max(0, ruleC->getMonthlyBuyLimit() - craftPurchaseLimitLog[ruleC->getType()] - getRow().amount);
				if (maxByLimit <= 0)
				{
					errorMessage = tr("STR_MONTHLY_CRAFT_PURCHASE_LIMIT_EXCEEDED");
				}
			}
			break;
		}			
		case TRANSFER_ITEM:
		{
			rule = (RuleItem*)getRow().rule;
			if (_base->storesOverfull(_iQty + rule->getSize()))
			{
				errorMessage = tr("STR_NOT_ENOUGH_STORE_SPACE");
			}
			else if (rule->isAlien())
			{
				int p = rule->getPrisonType();
				if (_iPrisonQty[p] + 1 > _base->getAvailableContainment(p) - _base->getUsedContainment(p))
				{
					errorMessage = trAlt("STR_NOT_ENOUGH_PRISON_SPACE", p);
				}
			}
			else if (rule->getMonthlyBuyLimit() > 0)
			{
				auto& itemPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
				int maxByLimit = std::max(0, rule->getMonthlyBuyLimit() - itemPurchaseLimitLog[rule->getType()] - getRow().amount);
				if (maxByLimit <= 0)
				{
					errorMessage = tr("STR_MONTHLY_ITEM_PURCHASE_LIMIT_EXCEEDED");
				}
			}
			break;
		}
		}
	}

	if (errorMessage.empty())
	{
		int maxByMoney = (_game->getSavedGame()->getFunds() - _total) / getRow().cost;
		if (maxByMoney >= 0)
			change = std::min(maxByMoney, change);
		switch (getRow().type)
		{
		case TRANSFER_SOLDIER:
			{
				RuleSoldier *ruleS = (RuleSoldier*)getRow().rule;
				if (ruleS->getMonthlyBuyLimit() > 0)
				{
					auto& soldierHireLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
					int maxByLimit = std::max(0, ruleS->getMonthlyBuyLimit() - soldierHireLimitLog[ruleS->getType()] - getRow().amount);
					change = std::min(maxByLimit, change);
				}
			}
			// fall-through
		case TRANSFER_SCIENTIST:
		case TRANSFER_ENGINEER:
			{
				int maxByQuarters = _base->getAvailableQuarters() - _base->getUsedQuarters() - _pQty;
				change = std::min(maxByQuarters, change);
				_pQty += change;
			}
			break;
		case TRANSFER_CRAFT:
			{
				RuleCraft *ruleC = (RuleCraft*)getRow().rule;
				if (ruleC->getMonthlyBuyLimit() > 0)
				{
					auto& craftPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
					int maxByLimit = std::max(0, ruleC->getMonthlyBuyLimit() - craftPurchaseLimitLog[ruleC->getType()] - getRow().amount);
					change = std::min(maxByLimit, change);
				}
				int p = ruleC->getHangarType();	
				int maxByHangars = _base->getAvailableHangars(p) - _base->getUsedHangars(p) - _tCQty[p];
				change = std::min(maxByHangars, change);	
				_tCQty[p] += change;
			}
			break;
		case TRANSFER_ITEM:
			{
				RuleItem *rule = (RuleItem*)getRow().rule;
				if (rule->getMonthlyBuyLimit() > 0)
				{
					auto& itemPurchaseLimitLog = _game->getSavedGame()->getMonthlyPurchaseLimitLog();
					int maxByLimit = std::max(0, rule->getMonthlyBuyLimit() - itemPurchaseLimitLog[rule->getType()] - getRow().amount);
					change = std::min(maxByLimit, change);
				}
				int p = rule->getPrisonType();
				if (rule->isAlien())
				{
					int maxByPrisons = _base->getAvailableContainment(p) - _base->getUsedContainment(p) - _iPrisonQty[p];
					change = std::min(maxByPrisons, change);
				}
				// both aliens and items
				{
					double storesNeededPerItem = rule->getSize();
					double freeStores = _base->getAvailableStores() - _base->getUsedStores() - _iQty;
					double maxByStores = (double)(INT_MAX);
					if (!AreSame(storesNeededPerItem, 0.0) && storesNeededPerItem > 0.0)
					{
						maxByStores = (freeStores + 0.05) / storesNeededPerItem;
					}
					change = std::min((int)maxByStores, change);
					_iQty += change * storesNeededPerItem;
				}
				if (rule->isAlien())
				{
					_iPrisonQty[p] += change;
				}
			}
			break;
		}
		getRow().amount += change;
		_total += getRow().cost * change;
		updateItemStrings();
	}
	else
	{
		_timerInc->stop();
		RuleInterface *menuInterface = _game->getMod()->getInterface("buyMenu");
		_game->pushState(new ErrorMessageState(errorMessage, _palette, menuInterface->getElement("errorMessage")->color, "BACK13.SCR", menuInterface->getElement("errorPalette")->color));
	}
}

/**
 * Decreases the quantity of the selected item to buy by one.
 */
void PurchaseState::decrease()
{
	_timerInc->setInterval(50);
	_timerDec->setInterval(50);
	decreaseByValue(_game->getScrollStep());
}

/**
 * Decreases the quantity of the selected item to buy by "change".
 * @param change how much we want to add.
 */
void PurchaseState::decreaseByValue(int change)
{
	if (0 >= change || 0 >= getRow().amount) return;
	change = std::min(getRow().amount, change);

	RuleItem *rule = nullptr;
	RuleCraft *ruleC = nullptr;		
	switch (getRow().type)
	{
	case TRANSFER_SOLDIER:
	case TRANSFER_SCIENTIST:
	case TRANSFER_ENGINEER:
		_pQty -= change;
		break;
	case TRANSFER_CRAFT:
		ruleC = (RuleCraft*)getRow().rule;
		_tCQty[ruleC->getHangarType()] -= change;	
		break;
	case TRANSFER_ITEM:
		rule = (RuleItem*)getRow().rule;
		_iQty -= rule->getSize() * change;
		if (rule->isAlien())
		{
			_iPrisonQty[rule->getPrisonType()] -= change;
		}
		break;
	}
	getRow().amount -= change;
	_total -= getRow().cost * change;
	updateItemStrings();
}

/**
 * Updates the quantity-strings of the selected item.
 */
void PurchaseState::updateItemStrings()
{
	_txtPurchases->setText(tr("STR_COST_OF_PURCHASES").arg(Unicode::formatFunding(_total)));
	std::ostringstream ss, ss5;
	ss << getRow().amount;
	_lstItems->setCellText(_sel, 3, ss.str());
	if (getRow().amount > 0)
	{
		_lstItems->setRowColor(_sel, _lstItems->getSecondaryColor());
	}
	else
	{
		_lstItems->setRowColor(_sel, _lstItems->getColor());
		if (getRow().type == TRANSFER_ITEM)
		{
			RuleItem *rule = (RuleItem*)getRow().rule;
			if (rule->getBattleType() == BT_AMMO || (rule->getBattleType() == BT_NONE && rule->getClipSize() > 0))
			{
				_lstItems->setRowColor(_sel, _ammoColor);
			}
		}
	}
	ss5 << _base->getUsedStores();
	if (std::abs(_iQty) > 0.05)
	{
		ss5 << "(";
		if (_iQty > 0.05)
			ss5 << "+";
		ss5 << std::fixed << std::setprecision(1) << _iQty << ")";
	}
	ss5 << ":" << _base->getAvailableStores();
	_txtSpaceUsed->setText(tr("STR_SPACE_USED").arg(ss5.str()));
}

/**
 * Updates the production list to match the category filter.
 */
void PurchaseState::cbxCategoryChange(Action *)
{
	updateList();
}

}
