<?php
/*
    Copyright (C) 2016 Volker Krause <vkrause@kde.org>

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

require_once('../../server/shared/product.php');
require_once('../../server/shared/schemaentry.php');

class UtilTest extends PHPUnit_Framework_TestCase
{
    public function testDataTableName_data()
    {
        return [
            'normal' => [ 'foo', 'product_org_kde_TestProduct_foo' ],
            'dot' => [ 'my.value', 'product_org_kde_TestProduct_my_value' ]
        ];
    }

    /** @dataProvider testDataTableName_data */
    public function testDataTableName($input, $output)
    {
        $p = new Product;
        $p->name = 'org.kde.TestProduct';
        $se = new SchemaEntry($p);
        $se->name = $input;
        $this->assertEquals($output, $se->dataTableName());
    }

    public function testInvalidJson_data()
    {
        return [
            'empty' => [ '{}' ],
            'empty name' => [ '{ "name": "", "type": "scalar", "aggregationType": "none" }' ],
            'empty type' => [ '{ "name": "foo", "type": "", "aggregationType": "none" }' ],
            'invalid type' => [ '{ "name": "foo", "type": "bla", "aggregationType": "none" }' ],
            'invalid name' => [ '{ "name": " foo ", "type": "scalar", "aggregationType": "none" }' ],
            'invalid name 2' => [ '{ "name": "1foo ", "type": "scalar", "aggregationType": "none" }' ]
        ];
    }

    /**
     * @dataProvider testInvalidJson_data
     * @expectedException RESTException
     * @exceptedExceptionCode 400
     */
    public function testInvalidJson($input)
    {
        $p = new Product;
        $se = SchemaEntry::fromJson(json_decode('[ ' . $input . ' ]'), $p);
    }
}
