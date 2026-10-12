#pragma once

#include "IFilterFactory.h"

class VUMeterFilterFactory : public IFilterFactory
{
public:
    void initialize(FilterEngine* value) override {engine=value;}
	std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;
private:
    FilterEngine* engine=nullptr;
};
