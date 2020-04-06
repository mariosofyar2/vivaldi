// Copyright (c) 2013 Vivaldi Technologies AS. All rights reserved

#include "components/keyed_service/content/browser_context_keyed_base_factory.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

template <typename T> struct DefaultSingletonTraits;

namespace Vivaldi {

class Notes_Model;

class NotesModelFactory : public BrowserContextKeyedServiceFactory
{
public:
  static Notes_Model* GetForProfile(Profile* profile);

  static Notes_Model* GetForProfileIfExists(Profile* profile);

  static NotesModelFactory* GetInstance();

  int64 get_current_id_max() { return current_max_id_; }

private:
	friend struct DefaultSingletonTraits<NotesModelFactory>;

	NotesModelFactory();
	~NotesModelFactory() override;

	// BrowserContextKeyedServiceFactory:
	KeyedService* BuildServiceInstanceFor(
				content::BrowserContext* context) const override;
	void RegisterProfilePrefs(
				user_prefs::PrefRegistrySyncable* registry) override;
	content::BrowserContext* GetBrowserContextToUse(
				content::BrowserContext* context) const override;
	bool ServiceIsNULLWhileTesting() const override;

  int64 current_max_id_;

DISALLOW_COPY_AND_ASSIGN(NotesModelFactory);

};

}
