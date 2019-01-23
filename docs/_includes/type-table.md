<ul>
{% for field in include.fields %}
<li><code>{{ field.type }}</code> <strong>{{ field.name }}</strong> - {{ field.desc }}</li>
{% endfor %}
</ul>
