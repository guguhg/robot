<script setup lang="ts">
import { company, siteNavigation } from '~/data/site-content'

const year = new Date().getFullYear()
</script>

<template>
  <footer class="corp-footer">
    <section class="corp-footer__lead corp-container">
      <span class="corp-footer__lead-tag">BUSINESS COOPERATION</span>
      <p>言明则意合，意合则事成。</p>
      <NuxtLink to="/about#contact">联系我们 <i aria-hidden="true">→</i></NuxtLink>
    </section>

    <div class="corp-footer__directory corp-container">
      <section class="corp-footer__column corp-footer__column--about">
        <h2>关于企业</h2>
        <p class="corp-footer__company">{{ company.name }}</p>
        <dl class="corp-footer__facts">
          <div><dt>成立日期</dt><dd>{{ company.registrationDate }}</dd></div>
          <div><dt>统一社会信用代码</dt><dd>{{ company.creditCode }}</dd></div>
          <div><dt>注册资本</dt><dd>{{ company.registration.capital }}</dd></div>
          <div><dt>企业类型</dt><dd>{{ company.registration.type }}</dd></div>
        </dl>
        <address class="corp-footer__address">
          <strong>{{ company.buildingName }}</strong>
          <p>{{ company.address }} · 邮编 {{ company.postalCode }}</p>
        </address>
      </section>

      <section class="corp-footer__column">
        <h2>商务联系</h2>
        <dl class="corp-footer__contact">
          <div><dt>联系电话</dt><dd>{{ company.contact.phone }}</dd></div>
          <div><dt>电子邮箱</dt><dd>{{ company.contact.email }}</dd></div>
          <div><dt>商务时间</dt><dd>{{ company.contact.businessHours }}</dd></div>
        </dl>
      </section>

      <section class="corp-footer__column">
        <h2>网站导航</h2>
        <nav class="corp-footer__nav" aria-label="页脚导航">
          <NuxtLink v-for="item in siteNavigation" :key="item.to" :to="item.to">
            {{ item.label }}
          </NuxtLink>
        </nav>
      </section>
    </div>

    <div class="corp-footer__legal corp-container">
      <span>© {{ year }} {{ company.name }} 版权所有</span>
      <div class="corp-footer__legal-links">
        <span>隐私政策 xxx</span>
        <span>ICP备案 {{ company.filing.icp }}</span>
        <span>公安备案 {{ company.filing.publicSecurity }}</span>
      </div>
    </div>
  </footer>
</template>
